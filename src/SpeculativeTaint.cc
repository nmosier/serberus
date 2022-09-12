#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Support/CommandLine.h>

#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>
#include <variant>

#include "min-cut.h"
#include "util.h"
#include "Transmitter.h"
#include "CommandLine.h"
#include "Mitigation.h"
#include "Log.h"

namespace clou {

// constexpr bool emit_dot = true;
constexpr bool debug = false;

struct ValueNode {
    enum class Kind { transmitter, source, interior };
    Kind kind;
    llvm::Value *V;
    
    auto to_tuple() const { return std::make_tuple(kind, V); }
    
    bool operator<(const ValueNode &o) const { return to_tuple() < o.to_tuple(); }
  bool operator==(const ValueNode& o) const { return to_tuple() == o.to_tuple(); }
};

struct SuperNode {
    enum class Kind { transmitter, source };
    Kind kind;
    
    auto to_tuple() const { return std::make_tuple(kind); }
    
    bool operator<(const SuperNode &o) const { return to_tuple() < o.to_tuple(); }
  bool operator==(const SuperNode& o) const { return to_tuple() == o.to_tuple(); }

};

using Node = std::variant<ValueNode, SuperNode>;
using Value = Node;

std::ostream &operator<<(std::ostream &os, ValueNode::Kind VNK) {
    using K = ValueNode::Kind;
    switch (VNK) {
        case K::source:
            os << "source";
            break;
        case K::transmitter:
            os << "transmitter";
            break;
        case K::interior:
            os << "interior";
            break;
    }
    return os;
}

std::ostream &operator<<(std::ostream &os, SuperNode::Kind SNK) {
    using K = SuperNode::Kind;
    switch (SNK) {
        case K::source:
            os << "source";
            break;
        case K::transmitter:
            os << "transmitter";
            break;
    }
    return os;
}

std::ostream &operator<<(std::ostream &os, const Value &V) {
    std::visit(util::overloaded{[&os](const ValueNode &VN) { os << VN.kind << *VN.V; },
                   [&os](const SuperNode &SN) { os << SN.kind; }},
               V);
    return os;
}

struct SpeculativeTaint final : public llvm::FunctionPass {
    static inline char ID = 0;
    static inline constexpr const char speculative_secret_label[] = "specsec";
    
    SpeculativeTaint() : llvm::FunctionPass(ID) {}
    
    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
        AU.addRequired<llvm::AAResultsWrapperPass>();
    }
    
    virtual bool runOnFunction(llvm::Function &F) override {
      FunctionLogger logger(F, ".", "SpeculativeTaint");
      
        llvm::AliasAnalysis &AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();

        /* Propogate taint rules */
        propogate_taint(F, AA);
        
        return true;
    }

    void propogate_taint(llvm::Function &F, llvm::AliasAnalysis &AA) const {
      llvm::DominatorTree DT(F);
      llvm::LoopInfo LI(DT);
      
        bool changed;

	// Get set of never-taint instructions. These are populated from results of optimization analyses.
	std::set<const llvm::Instruction *> secure;
	for (llvm::BasicBlock& B : F) {
	  for (llvm::Instruction& I : B) {
	    if (I.getMetadata("clou.secure")) {
	      secure.insert(&I);
	    }
	  }
	}
        
        /* Approach:
         * Update taint as we go.
         * Maintain map of taint mems.
         *
         */
        // tainted instructions, initially empty
        std::set<llvm::Instruction *> taints, taints_bak;
        // tainted memory, initially empty
	std::set<llvm::StoreInst *> mem, mem_bak;
	std::map<llvm::LoadInst *, std::set<llvm::StoreInst *>> rfs, rfs_bak;
        
        std::set<llvm::LoadInst *> sources;
        
        do {
            changed = false;
            
            for (llvm::BasicBlock &B : F) {
                for (llvm::Instruction &I : B) {
                    if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
                        if (has_incoming_addr(LI->getPointerOperand())) {
                            // only speculatively taint if the value returned by the load isn't already a secret
                            if (!is_nonspeculative_secret(LI)) {
                                // address dependency: always tainted
                                taints.insert(LI);
                                sources.insert(LI);
                            }
                        } else {
                            // check if it must overlap with a public store
                            for (llvm::StoreInst *SI : mem) {
                                const llvm::Value *P = SI->getPointerOperand();
                                const llvm::AliasResult alias = AA.alias(P, LI->getPointerOperand());
				if (alias != llvm::AliasResult::NoAlias) {
				  rfs[LI].insert(SI);
				  taints.insert(LI);
                                }
                            }
                        }
                    } else if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
                        
                        // check if value operand is tainted
                        llvm::Value *V = SI->getValueOperand();
                        llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V);
                        bool tainted = I && taints.contains(I);
                        if (tainted) {
                            mem.insert(SI);
                        } else {
                            // remove stores that have been overwritten with public values
                            for (auto it = mem.begin(); it != mem.end();) {
                                if (AA.alias(SI, *it) == llvm::AliasResult::MustAlias) {
                                    it = mem.erase(it);
                                } else {
                                    ++it;
                                }
                            }
                        }
                        
                    } else if (llvm::isa<llvm::CallBase>(&I)) {
                        
                        // ignore: functions will never return speculatively tainted values (this is an invariant we must uphold)
                        
                    } else if (llvm::isa<llvm::FenceInst, MitigationInst>(&I)) {
                        
                        // ignore: we deal with them later on during graph analysis
                        
                    } else if (!I.getType()->isVoidTy()) {
                        
                        // taint if any of inputs are tainted
                        bool tainted = std::any_of(
                                                   I.op_begin(), I.op_end(), [&](llvm::Value *V) -> bool {
                                                       if (llvm::Instruction *I =
                                                           llvm::dyn_cast<llvm::Instruction>(V)) {
                                                           if (taints.contains(I)) {
                                                               return true;
                                                           }
                                                       }
                                                       return false;
                                                   });
                        if (tainted) {
                            taints.insert(&I);
                        }
                    }
                }
            }

            changed = (taints != taints_bak || mem != mem_bak || rfs != rfs_bak);
            
            taints_bak = taints;
	    mem_bak = mem;
            rfs_bak = rfs;
            
        } while (changed);
        
        // gather transmitters
        std::map<llvm::Instruction *, std::set<llvm::Instruction *>> transmitters;
        for_each_inst<llvm::Instruction>(F, [&](llvm::Instruction *I) {
	  if (!secure.contains(I)) {
            const auto ops = get_transmitter_sensitive_operands(I);
            std::set<llvm::Instruction *> tainted_ops;
            for (const TransmitterOperand& op : ops) {
	      llvm::Value *V = op.V;
	      if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
		if (taints.contains(I)) {
		  tainted_ops.insert(I);
		}
	      }
            }
            if (!tainted_ops.empty()) {
	      transmitters[I] = std::move(tainted_ops);
            }
	  }
        });
        
        using Alg = FordFulkersonMulti<ValueNode, int>;
        Alg::Graph G;
        
        // add CFG
        for_each_inst<llvm::Instruction>(F, [&](llvm::Instruction *dst) {
            for (llvm::Instruction *src : llvm::predecessors(dst)) {
                ValueNode src_VN = {.kind = ValueNode::Kind::interior, .V = src};
                ValueNode dst_VN = {.kind = ValueNode::Kind::interior, .V = dst};
                // TODO: weight edges
                G[src_VN][dst_VN] = compute_edge_weight(dst, DT, LI);
            }
        });

	/* Add edge connecting return instructions to entry instructions. 
	 * This allows us to capture aliases across invocations of the same function.
	 */
	for_each_inst<llvm::ReturnInst>(F, [&](llvm::ReturnInst *RI) {
	  ValueNode src_VN = {.kind = ValueNode::Kind::interior, .V = RI};
	  llvm::Instruction *entry = &F.getEntryBlock().front();
	  ValueNode dst_VN = {.kind = ValueNode::Kind::interior, .V = entry};
	  G[src_VN][dst_VN] = compute_edge_weight(entry, DT, LI);
	});
        
        // transmitters
        for (const auto& [transmitter, _] : transmitters) {
            ValueNode src_VN = {.kind = ValueNode::Kind::interior, .V = transmitter};
            ValueNode dst_VN = {.kind = ValueNode::Kind::transmitter,
                .V = transmitter};
            G[src_VN][dst_VN] = compute_edge_weight(transmitter, DT, LI);
        }
        
        // sources
        for (llvm::Value *source : sources) {
            ValueNode src_VN = {.kind = ValueNode::Kind::source, .V = source};
            ValueNode dst_VN = {.kind = ValueNode::Kind::interior, .V = source};
            G[src_VN][dst_VN] = compute_edge_weight(llvm::cast<llvm::Instruction>(source)->getNextNode(), DT, LI);
        }
        
        for_each_inst<MitigationInst>(F, [&](MitigationInst *FI) {
            for (llvm::Instruction *pred : llvm::predecessors(FI)) {
                ValueNode src = {.kind = ValueNode::Kind::interior, .V = pred};
                ValueNode dst = {.kind = ValueNode::Kind::interior, .V = FI};
                G[src].erase(dst);
            }
        });
        
        // add supersource and supertransmitter nodes
        std::map<Node, std::size_t> nodes;
        for (const auto& src : G) {
            nodes.emplace(src.first, nodes.size());
            for (const auto& dst : src.second) {
                nodes.emplace(dst.first, nodes.size());
            }
        }
        
        // source-transmitter pairs
        std::vector<Alg::ST> sts;
        const auto get_transmitter_sources = [&] (llvm::Value *V, auto out) {
            std::queue<llvm::Value *> queue;
            std::set<llvm::Value *> seen;
            queue.push(V);
            
            while (!queue.empty()) {
                llvm::Value *V = queue.front();
                queue.pop();
                if (!seen.insert(V).second) {
                    continue;
                }
                
                if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(V)) {
                    if (sources.contains(LI)) {
                        *out++ = LI;
                    } else {
		      for (llvm::StoreInst *SI : rfs[LI]) {
			queue.push(SI->getValueOperand());
		      }
                    }
                } else if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
                    if (!I->getType()->isVoidTy()) {
                        for (llvm::Value *V : get_incoming_loads(I)) {
                            if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(V)) {
                                queue.push(LI);
                            }
                        }
                    }
                }
            }
        };
        
        for (const auto& [T, tainted_ops] : transmitters) {
            std::set<llvm::Instruction *> sources;
            for (llvm::Value *tainted_op : tainted_ops) {
                get_transmitter_sources(tainted_op, std::inserter(sources, sources.end()));
            }
            ValueNode dst = {
                .kind = ValueNode::Kind::transmitter,
                .V = T
            };
            Alg::ST st = {.t = dst};
            for (llvm::Instruction *source : sources) {
                ValueNode src = {
                    .kind = ValueNode::Kind::source,
                    .V = source
                };
                st.s.insert(src);
            }
            sts.push_back(st);
        }
        
        std::vector<std::pair<Node, Node>> cut_edges;
        Alg::run(G, sts.begin(), sts.end(), std::back_inserter(cut_edges));
        
        // output dot graph, color cut edges
        if (!emit_dot.getValue().empty()) {
            std::stringstream path;
            path << emit_dot.getValue() << "/" << F.getName().str() << ".dot";
            std::ofstream f(path.str());
            f << "digraph {\n";
            
            for (const auto& [node, i] : nodes) {
                f << "node" << i << " [label=\"" << node << "\"";
                
                const char *color = std::visit(
                                               util::overloaded{
                                                   [](const ValueNode &VN) -> const char * {
                                                       return VN.kind != ValueNode::Kind::interior ? "blue"
                                                       : nullptr;
                                                   },
                                                   [](const SuperNode &) -> const char * { return "blue"; }},
                                               node);
                if (color) {
                    f << ", color=\"" << color << "\"";
                }
                f << "];\n";
            }
            
            for (const auto& [u, usucc] : G) {
                for (const auto& [v, weight] : usucc) {
                    if (weight > 0) {
                        f << "node" << nodes.at(u) << " -> " << "node" << nodes.at(v) << " [label=\"" << weight << "\"";
                        if (std::find(cut_edges.begin(), cut_edges.end(), std::pair<Node, Node>(u, v)) != cut_edges.end()) {
                            f << ", color=\"red\"";
                        }
                        f << "];\n";
                    }
                }
            }
            
            f << "}\n";
            
            // output edges
        }

        // mitigate edges
        for (const auto& [src, dst] : cut_edges) {
            if (debug) {
                llvm::errs() << "cut edge:\n";
                llvm::errs() << util::to_string(src) << "\n"
                << util::to_string(dst) << "\n\n";
            }
            
            const auto &VN = std::get<ValueNode>(dst);
            
            llvm::Instruction *P = llvm::cast<llvm::Instruction>(VN.V);
            if (std::get<ValueNode>(src).kind == ValueNode::Kind::source) {
                P = P->getNextNode();
                assert(P);
            }
	    CreateMitigation(P, "udt-mitigation");
	}
    }
    
    static llvm::FunctionCallee get_clou_rf_intrinsic(llvm::Module &M) {
        llvm::LLVMContext &C = M.getContext();
        constexpr const char *clou_rf_intrinsic_name = "clou.rf";
        return M.getOrInsertFunction(
                                     clou_rf_intrinsic_name,
                                     llvm::FunctionType::get(llvm::Type::getVoidTy(C), true));
    }
    
    static void add_taint(llvm::Instruction *I) {
        llvm::LLVMContext &ctx = I->getContext();
        I->setMetadata("taint", llvm::MDNode::get(ctx, llvm::ArrayRef<llvm::Metadata *>(llvm::MDString::get(    I->getContext(), speculative_secret_label))));
    }
    
    static unsigned compute_edge_weight(llvm::Instruction *I, const llvm::DominatorTree& DT, const llvm::LoopInfo& LI) {
        float score = 1.;
        score *= instruction_loop_nest_depth(I, LI) + 1;
        score *= 1. / (instruction_dominator_depth(I, DT) + 1);
        return score * 100;
    }
};

namespace {
llvm::RegisterPass<SpeculativeTaint> X{"clou-mitigate",
				       "Clou Mitigation Pass"};
  util::RegisterClangPass<SpeculativeTaint> Y;
}
}
