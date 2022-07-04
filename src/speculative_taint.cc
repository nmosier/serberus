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

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/one_bit_color_map.hpp>
#include <boost/graph/stoer_wagner_min_cut.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/typeof/typeof.hpp>

#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

#include "util.h"

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
              // address dependency: always tainted
              taints.insert(LI);
              sources.insert(LI);
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
          } else if (llvm::StoreInst *SI =
                         llvm::dyn_cast<llvm::StoreInst>(&I)) {

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

    // generate graph
    enum { TRANSMITTER, OTHER };
    using Graph =
        boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS,
                              boost::no_property,
                              boost::property<boost::edge_weight_t, int>>;
    using WeightMap = boost::property_map<Graph, boost::edge_weight_t>::type;
    using Weight = boost::property_traits<WeightMap>::value_type;
    std::vector<std::pair<int, int>> edges;
    std::vector<float_t> edge_weights;

    std::map<std::pair<llvm::Value *, bool>, int> inst_to_vertex;
    std::vector<std::pair<llvm::Value *, bool>> vertex_to_inst;
    const auto get_vertex = [&inst_to_vertex, &vertex_to_inst](
                                std::pair<llvm::Value *, bool> I) -> int {
      const auto res = inst_to_vertex.emplace(I, inst_to_vertex.size());
      if (res.second) {
        vertex_to_inst.push_back(I);
      }
      return res.first->second;
    };
    const auto add_edge = [&get_vertex, &edges, &edge_weights](
                              std::pair<llvm::Value *, bool> src,
                              std::pair<llvm::Value *, bool> dst,
                              float_t weight) {
      edges.emplace_back(get_vertex(src), get_vertex(dst));
      edge_weights.push_back(weight);
    };

    // add "addr" edges
    for (llvm::Instruction *transmitter : transmitters) {
      for (llvm::Value *leaked_V :
           get_transmitter_sensitive_operands(transmitter)) {
        for (llvm::Value *src : get_incoming_loads(leaked_V)) {
          if (get_tainted(src)) {
            add_edge(std::make_pair(llvm::cast<llvm::Instruction>(src), false),
                     std::make_pair(transmitter, true), 1.f);
          }
        }
      }
    }

    // add "rf" edges
    for (const auto &p : shared_rfs) {
      llvm::LoadInst *LI = p.first;
      for (llvm::StoreInst *SI : p.second) {
        add_edge(std::make_pair(SI, false), std::make_pair(LI, false), 1.f);
        // add "data" edges
        for (llvm::Value *V : get_incoming_loads(SI->getValueOperand())) {
          if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
            add_edge(std::make_pair(I, false), std::make_pair(SI, false), 1.f);
          }
        }
      }
    }
      
#if 1
      // add Spectre v1.1 edges
      for_each_inst<llvm::StoreInst>(F, [&] (llvm::StoreInst *SI) {
          if (has_incoming_addr(SI->getPointerOperand())) {
              llvm::Value *V = SI->getValueOperand();
              if (is_nonspeculative_secret(V) || get_tainted(V)) {
                  llvm::Instruction *I = llvm::cast<llvm::Instruction>(V);
                  // add as transmitter
                  transmitters.insert(I);
                  
                  // add "addr" edges
                  for (llvm::Value *V : get_incoming_loads(SI->getPointerOperand())) {
                      sources.insert(V);
                      add_edge(std::make_pair(V, false), std::make_pair(I, true), 1.f);
                  }
              }
          }
      });
#endif
      
      // trim graph
      while (true) {
          std::vector<unsigned> outs (vertex_to_inst.size(), 0);
          for (const auto& edge : edges) {
              outs[edge.first]++;
          }
          
          bool erased = false;
          for (std::size_t i = 0; i < outs.size(); ++i) {
              const auto& vert = vertex_to_inst.at(i);
                // llvm::errs() << outs[edge.second] << " " << *vertex_to_inst.at(i).first << " " << sources.contains(vert.first) << "\n";
              if (outs[i] == 0 && !(vert.second && transmitters.contains(llvm::dyn_cast<llvm::Instruction>(vert.first))) && !sources.contains(vert.first)) {
                  if (std::erase_if(edges, [&] (const auto& edge) {
                      return edge.second == i;
                  }) > 0) {
                      erased = true;
                      // llvm::errs() << "here\n";
                      sources.insert(vert.first);
                      break;
                  }
              }
          }
          
          if (!erased) {
              break;
          }
      }
      
      // add infinite edges among all sources and among all transmitters
      const auto add_infinite_edges = [&add_edge](const auto &set,
                                                  bool transmitter) {
        for (auto it1 = set.begin(); it1 != set.end(); ++it1) {
          for (auto it2 = std::next(it1); it2 != set.end(); ++it2) {
            add_edge(std::make_pair(*it1, transmitter),
                     std::make_pair(*it2, transmitter), 1000.f);
          }
        }
      };
      add_infinite_edges(sources, false);
      add_infinite_edges(transmitters, true);
      
      if (edges.empty()) {
          return;
      }

    Graph graph(edges.begin(), edges.end(), edge_weights.begin(),
                vertex_to_inst.size(), edges.size());

    const auto parities = boost::make_one_bit_color_map(
        boost::num_vertices(graph), boost::get(boost::vertex_index, graph));
    boost::stoer_wagner_min_cut(graph, boost::get(boost::edge_weight, graph),
                                boost::parity_map(parities));

    // output dot graph, color cut edges
    {
      std::stringstream path;
      path << std::getenv("OUT") << "/" << F.getName().str() << ".dot";
      std::ofstream f(path.str());
      f << "digraph {";
      for (std::size_t i = 0; i < vertex_to_inst.size(); ++i) {
        const char *color = boost::get(parities, i) ? "red" : "blue";
        const char *kind = nullptr;
        if (transmitters.contains(llvm::cast<llvm::Instruction>(vertex_to_inst.at(i).first)) &&
            vertex_to_inst.at(i).second) {
          kind = "transmitter";
        } else if (sources.contains(llvm::dyn_cast<llvm::LoadInst>(
                       vertex_to_inst.at(i).first))) {
          kind = "source";
        }
        f << "node" << i << " [label=\"";
        if (kind) {
          f << kind << "\n";
        }
        f << util::to_string(*vertex_to_inst.at(i).first) << "\", color=\""
          << color << "\"];\n";
      }
      for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto &edge = edges.at(i);
        f << "node" << edge.first << " -> "
          << "node" << edge.second;
        f << "[label=\"" << edge_weights.at(i) << "\"";
        if (boost::get(parities, edge.first) !=
            boost::get(parities, edge.second)) {
          f << ", color=\"red\"";
        }
        f << "];\n";
      }
      f << "}\n";

      // output edges
    }

    // mitigate edges
    for (const auto &edge : edges) {
      if (boost::get(parities, edge.first) !=
          boost::get(parities, edge.second)) {
        llvm::errs() << "cut edge:\n";
        llvm::errs() << *vertex_to_inst.at(edge.first).first << "\n";
        llvm::errs() << *vertex_to_inst.at(edge.second).first << "\n";

        llvm::IRBuilder<> IRB(llvm::cast<llvm::Instruction>(vertex_to_inst.at(edge.second).first));
        IRB.CreateFence(llvm::AtomicOrdering::Acquire);
      }
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
