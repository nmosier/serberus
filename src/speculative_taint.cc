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

#if 0
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/one_bit_color_map.hpp>
#include <boost/graph/stoer_wagner_min_cut.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/typeof/typeof.hpp>
#endif

#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

#include "graph.h"
#include "min-cut.h"
#include "util.h"

constexpr bool emit_dot = false;
constexpr bool debug = false;

struct ValueNode {
  enum class Kind { transmitter, source, interior };
  Kind kind;
  llvm::Value *V;

  auto to_tuple() const { return std::make_tuple(kind, V); }

  bool operator<(const ValueNode &o) const { return to_tuple() < o.to_tuple(); }
};

struct SuperNode {
  enum class Kind { transmitter, source };
  Kind kind;

  auto to_tuple() const { return std::make_tuple(kind); }

  bool operator<(const SuperNode &o) const { return to_tuple() < o.to_tuple(); }
};

using Node = std::variant<ValueNode, SuperNode>;
using Value = Node;

struct NodeCmp {
  bool operator()(const Node &a, const Node &b) const { return a < b; }
};

#if 0
bool operator<(ValueNode::Kind a, ValueNode::Kind b) {
    using T = std::underlying_type_t<ValueNode::Kind>;
    return static_cast<T>(a) < static_cast<T>(b);
}

bool operator<(SuperNode::Kind a, SuperNode::Kind b) {
    using T = std::underlying_type_t<ValueNode::Kind>;
    return static_cast<T>(a) < static_cast<T>(b);
}
#endif

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
  std::visit(
      util::overloaded{[&os](const ValueNode &VN) { os << VN.kind << *VN.V; },
                       [&os](const SuperNode &SN) { os << SN.kind; }},
      V);
  return os;
}

struct SpeculativeTaint final : public llvm::FunctionPass {
  static inline char ID = 0;

  static inline constexpr const char *speculative_secret_label = "specsec";

  SpeculativeTaint() : llvm::FunctionPass(ID) {}

  virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
    AU.addRequired<llvm::AAResultsWrapperPass>();
  }

  virtual bool runOnFunction(llvm::Function &F) override {
    // SpeculativeAliasAnalysis SAA
    // {getAnalysis<llvm::AAResultsWrapperPass>(F)};
    llvm::AliasAnalysis &AA =
        getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();

    /* Propogate taint rules */
    propogate_taint(F, AA);

    return true;
  }

  void propogate_taint(llvm::Function &F, llvm::AliasAnalysis &AA) const {
    bool changed;

    /* Approach:
     * Update taint as we go.
     * Maintain map of taint mems.
     *
     */
    // tainted instructions, initially empty
    std::set<llvm::Instruction *> taints, taints_bak;
    using Mem = std::set<llvm::StoreInst *>;
    // tainted memory, initially empty
    std::map<llvm::BasicBlock *, Mem> mems_in, mems_out, mems_in_bak;
    std::map<llvm::LoadInst *, std::set<llvm::StoreInst *>> shared_rfs,
        shared_rfs_bak;

    std::set<llvm::Value *> sources;

    const auto get_tainted = [&taints](llvm::Value *V) -> bool {
      return taints.contains(llvm::dyn_cast<llvm::Instruction>(V));
    };

    do {
      changed = false;

      for (llvm::BasicBlock &B : F) {
        Mem mem = std::move(mems_in[&B]);

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
              std::set<llvm::StoreInst *> rfs;
              llvm::AliasResult alias = llvm::AliasResult::NoAlias;
              for (llvm::StoreInst *SI : mem) {
                const llvm::Value *P = SI->getPointerOperand();
                const llvm::AliasResult cur_alias =
                    AA.alias(P, LI->getPointerOperand());
                alias = std::max(alias, cur_alias);
                if (cur_alias /* == llvm::AliasResult::MayAlias */ !=
                    llvm::AliasResult::NoAlias) {
                  rfs.insert(SI);
                }
              }

              if (alias != llvm::AliasResult::NoAlias) {
                taints.insert(LI);
                shared_rfs[LI] = std::move(rfs);
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
              
          } else if (llvm::isa<llvm::FenceInst>(&I)) {
              
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

        mems_out[&B] = std::move(mem);
      }

      /* meet */
      for (llvm::BasicBlock &B : F) {
        Mem &mem = mems_in[&B];
        for (llvm::BasicBlock *B_pred : llvm::predecessors(&B)) {
          const Mem &mem_pred = mems_out[B_pred];
          std::copy(mem_pred.begin(), mem_pred.end(),
                    std::inserter(mem, mem.end()));
        }
      }

      changed = (taints != taints_bak || mems_in != mems_in_bak ||
                 shared_rfs != shared_rfs_bak);

      taints_bak = taints;
      mems_in_bak = mems_in;
      shared_rfs_bak = shared_rfs;

    } while (changed);

    // gather transmitters
    std::set<llvm::Instruction *> transmitters;
    for_each_inst<llvm::Instruction>(F, [&transmitters,
                                         &taints](llvm::Instruction *I) {
      const auto ops = get_transmitter_sensitive_operands(I);
      const bool tainted_op =
          std::any_of(ops.begin(), ops.end(), [&taints](llvm::Value *V) {
            if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
              return taints.contains(I);
            }
            return false;
          });
      if (tainted_op) {
        transmitters.insert(I);
      }
    });

#if 0
        if (transmitters.empty()) {
            return;
        }
#endif

    Graph<Value, NodeCmp> G;

    // add CFG
    for_each_inst<llvm::Instruction>(F, [&](llvm::Instruction *dst) {
      for (llvm::Instruction *src : llvm::predecessors(dst)) {
        ValueNode src_VN = {.kind = ValueNode::Kind::interior, .V = src};
        ValueNode dst_VN = {.kind = ValueNode::Kind::interior, .V = dst};
        G.add_edge(src_VN, dst_VN, 1);
      }
    });

#if 0
    // add edges
    {
      std::vector<ValueNode> todo;
        std::set<ValueNode> seen;
      std::transform(transmitters.begin(), transmitters.end(),
                     std::back_inserter(todo), [](llvm::Instruction *T) {
                       ValueNode VN = {.kind = ValueNode::Kind::transmitter,
                                       .V = T};
                       return VN;
                     });

      while (!todo.empty()) {
        ValueNode VN = todo.back();
        todo.pop_back();
          
          if (!seen.insert(VN).second) {
              continue;
          }

        switch (VN.kind) {
        case ValueNode::Kind::transmitter: {
          llvm::Instruction *T = llvm::cast<llvm::Instruction>(VN.V);
          for (llvm::Value *leaked_V : get_transmitter_sensitive_operands(T)) {
            for (llvm::Value *src : get_incoming_loads(leaked_V)) {
              if (get_tainted(src)) {
                ValueNode src_VN = {.kind = ValueNode::Kind::interior,
                                    .V = src};
                if (sources.contains(src)) {
                  src_VN.kind = ValueNode::Kind::source;
                }
                G.add_edge(src_VN, VN, 1);
                todo.push_back(src_VN);
              }
            }
          }

        } break;

        case ValueNode::Kind::interior: {
          if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(VN.V)) {
            // trace back data sources
                for (llvm::Value *src : get_incoming_loads(SI->getValueOperand())) {
              if (get_tainted(src)) {
                ValueNode src_VN = {.kind = ValueNode::Kind::interior,
                                    .V = src};
                if (sources.contains(src)) {
                  src_VN.kind = ValueNode::Kind::source;
                }
                G.add_edge(src_VN, VN, 1);
                todo.push_back(src_VN);
              }
            }
          } else if (llvm::LoadInst *LI =
                         llvm::dyn_cast<llvm::LoadInst>(VN.V)) {
            // trace back rf edges
            for (llvm::StoreInst *SI : shared_rfs.at(LI)) {
              ValueNode src_VN = {.kind = ValueNode::Kind::interior, .V = SI};
              G.add_edge(src_VN, VN, 1);
              todo.push_back(src_VN);
            }
          } else {
            std::abort();
          }
        } break;

        case ValueNode::Kind::source: {
          // do nothing
        } break;
        }
      }
    }
#endif

    // add Spectre v1.1 edges
    for_each_inst<llvm::StoreInst>(F, [&](llvm::StoreInst *SI) {
      if (has_incoming_addr(SI->getPointerOperand())) {
        llvm::Value *op_V = SI->getValueOperand();
        if (is_nonspeculative_secret(op_V) || get_tainted(op_V)) {
          // add as transmitter
          transmitters.insert(SI);

          ValueNode dst_VN = {.kind = ValueNode::Kind::transmitter, .V = SI};

          // add addr edges
          for (llvm::Value *src_V : get_incoming_loads(op_V)) {
            sources.insert(src_V);
            ValueNode src_VN = {.kind = ValueNode::Kind::source, .V = src_V};
            G.add_edge(src_VN, dst_VN, 1);
          }
        }
      }
    });

    // transmitters
    for (llvm::Instruction *transmitter : transmitters) {
      ValueNode src_VN = {.kind = ValueNode::Kind::interior, .V = transmitter};
      ValueNode dst_VN = {.kind = ValueNode::Kind::transmitter,
                          .V = transmitter};
      G.add_edge(src_VN, dst_VN, 1);
    }

    // sources
    for (llvm::Value *source : sources) {
      ValueNode src_VN = {.kind = ValueNode::Kind::source, .V = source};
      ValueNode dst_VN = {.kind = ValueNode::Kind::interior, .V = source};
      G.add_edge(src_VN, dst_VN, 1);
    }

    for_each_inst<llvm::FenceInst>(F, [&](llvm::FenceInst *FI) {
      for (llvm::Instruction *pred : llvm::predecessors(FI)) {
        ValueNode src = {.kind = ValueNode::Kind::interior, .V = pred};
        ValueNode dst = {.kind = ValueNode::Kind::interior, .V = FI};
        G.remove_edge(src, dst);
      }
    });

    // add supersource and supertransmitter nodes
    const SuperNode supertransmitter = {.kind = SuperNode::Kind::transmitter};
    const SuperNode supersource = {.kind = SuperNode::Kind::source};
    G.add_node(supertransmitter);
    G.add_node(supersource);
    for (llvm::Instruction *T : transmitters) {
      ValueNode src_VN = {.kind = ValueNode::Kind::transmitter, .V = T};
      G.add_edge(src_VN, supertransmitter, 1000);
    }
    for (llvm::Value *S : sources) {
      ValueNode dst_VN = {.kind = ValueNode::Kind::source, .V = S};
      G.add_edge(supersource, dst_VN, 1000);
    }
      
      llvm::errs() << "min cut for " << F.getName() << ": " << G.num_vertices() << " vertices\n";

    const auto cut_edges =
        minCut(G.adjacency_array(), G.lookup_node(supersource),
               G.lookup_node(supertransmitter));

    if (debug) {
      for (const auto &cut_edge : cut_edges) {
        llvm::errs() << cut_edge.first << " " << cut_edge.second << "\n";
      }
    }

    // output dot graph, color cut edges
    if (emit_dot) {
      std::stringstream path;
      path << std::getenv("OUT") << "/" << F.getName().str() << ".dot";
      std::ofstream f(path.str());
      f << "digraph {";
      for (std::size_t i = 0; i < G.num_vertices(); ++i) {
        const auto &node = G.lookup_vert(i);

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

      for (decltype(G)::Vertex i = 0; i < G.num_vertices(); ++i) {
        for (decltype(G)::Vertex j = 0; j < G.num_vertices(); ++j) {
          if (auto w = G.get_edge_v(i, j)) {
            f << "node" << i << " -> node" << j << " [label=\"" << w << "\"";
            if (std::find(cut_edges.begin(), cut_edges.end(),
                          std::pair<int, int>(i, j)) != cut_edges.end()) {
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
    for (const auto &cut_edge : cut_edges) {
      const auto src = G.lookup_vert(cut_edge.first);
      const auto dst = G.lookup_vert(cut_edge.second);
      if (debug) {
        llvm::errs() << "cut edge:\n";
        llvm::errs() << util::to_string(src) << "\n"
                     << util::to_string(dst) << "\n\n";
      }

      const auto &VN = std::get<ValueNode>(dst);

      llvm::Instruction *P = llvm::cast<llvm::Instruction>(VN.V);
      std::optional<llvm::IRBuilder<>> IRB(P);
      if (std::get<ValueNode>(src).kind == ValueNode::Kind::source) {
        if (llvm::Instruction *P_ = P->getNextNode()) {
          IRB.emplace(P_);
        } else {
          IRB.emplace(P->getParent());
        }
      }

      IRB->CreateFence(llvm::AtomicOrdering::Acquire);
    }

#if 0
        // commit taint as metadata
        for (llvm::Instruction *I : taints) {
            add_taint(I);
            
            if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
                const auto it = shared_rfs.find(LI);
                if (it != shared_rfs.end()) {
#if 0
                    llvm::FunctionCallee intrin = get_clou_rf_intrinsic(*F.getParent());
                    llvm::IRBuilder<> IRB (LI->getNextNonDebugInstruction());
                    std::vector<llvm::Value *> args;
                    std::copy(it->second.begin(), it->second.end(), std::back_inserter(args));
                    IRB.CreateCall(intrin, args);
#elif 0
                    std::vector<llvm::Metadata *> sources;
                    for (llvm::StoreInst *SI : it->second) {
                        sources.push_back(llvm::ValueAsMetadata::get(SI->getPointerOperand()));
                    }
                    
                    llvm::MDTuple *tuple = llvm::MDTuple::get(F.getContext(), sources);
                    LI->setMetadata("clou.rf", tuple);
                    
                    //
#endif
                }
            }
        }
        
        // assign numbers to stores
        std::set<llvm::StoreInst *> stores;
        for (const auto& p : shared_rfs) {
            std::copy(p.second.begin(), p.second.end(), std::inserter(stores, stores.end()));
        }
        unsigned store_counter = 0;
        for (llvm::StoreInst *SI : stores) {
            llvm::MDString *MDS = llvm::MDString::get(F.getContext(), std::to_string(store_counter));
            llvm::MDNode *MD = llvm::MDNode::get(F.getContext(),
                                                 llvm::ArrayRef<llvm::Metadata *>(MDS));
            SI->setMetadata("clou.sid", MD);
            ++store_counter;
        }
        
        // emit rf metadata
        for (llvm::BasicBlock& B : F) {
            for (llvm::Instruction& I : B) {
                if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
                    const auto it = shared_rfs.find(LI);
                    if (it != shared_rfs.end() && !it->second.empty()) {
                        std::vector<llvm::Metadata *> Ms;
                        for (llvm::StoreInst *SI : it->second) {
                            llvm::MDNode *MD = SI->getMetadata("clou.sid");
                            assert(MD);
                            Ms.push_back(MD->getOperand(0));
                        }
                        LI->setMetadata("clou.rf", llvm::MDNode::get(F.getContext(), Ms));
                    }
                }
            }
        }
#endif
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
    I->setMetadata(
        "taint", llvm::MDNode::get(
                     ctx, llvm::ArrayRef<llvm::Metadata *>(llvm::MDString::get(
                              I->getContext(), speculative_secret_label))));
  }
};

namespace {
llvm::RegisterPass<SpeculativeTaint> X{"speculative-taint",
                                       "Speculative Taint Pass"};
}
