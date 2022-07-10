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
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/DependenceAnalysis.h>
#include <llvm/IR/Type.h>
#include <llvm/Analysis/CallGraphSCCPass.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/BasicAliasAnalysis.h>

#include <map>
#include <set>
#include <memory>
#include <forward_list>

#include "transmitter.h"
#include "my_scc_pass.h"

namespace {

struct SecretParamAnalysis final: public MySCCPass {
    static inline char ID = 0;
    SecretParamAnalysis(): MySCCPass(ID) {}
    
    virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
        AU.setPreservesAll();
        // llvm::getAAResultsAnalysisUsage(AU);
        // AU.addRequired<llvm::ScalarEvolutionWrapperPass>();
        // AU.addRequired<llvm::AAResultsWrapperPass>();
        // llvm::CallGraphSCCPass::getAnalysisUsage(AU);
        AU.addRequired<llvm::DependenceAnalysisWrapperPass>();
    }
    
    struct FunctionSummary {
        std::set<llvm::Argument *> pub_args;
        std::set<llvm::Argument *> ret_args;
    };
    
    std::map<llvm::Function *, FunctionSummary> summaries;
    
    virtual bool runOnSCC(const MySCC& SCC) override {
        std::set<llvm::Function *> Fs;
        std::transform(SCC.begin(), SCC.end(), std::inserter(Fs, Fs.end()), [] (llvm::CallGraphNode *CGN) {
            return CGN->getFunction();
        });
        
        for_each_func_def(Fs.begin(), Fs.end(), [&] (llvm::Function& F) {
            summaries[&F];
        });
        
        /* Task: compute the ret_args function summary of each function in the SCC.
         * 1. Compute reverse rfs
         * 2. Propogate return values.
         */
        
        // 1. intialize reverse rfs
        std::map<llvm::LoadInst *, llvm::StoreInst *> rfs;
        for_each_func_def(Fs.begin(), Fs.end(), [&] (llvm::Function& F) {
            llvm::DominatorTree DT (F);
            
#if 0
            llvm::AAResults& AA = getAnalysis<llvm::AAResultsWrapperPass>(F).getAAResults();
            llvm::ScalarEvolution& SE = getAnalysis<llvm::ScalarEvolutionWrapperPass>(F).getSE();
            llvm::LoopInfo LI (DT);
            llvm::DependenceInfo DI (&F, &AA, &SE, &LI);
#else
            llvm::DependenceInfo& DI = getAnalysis<llvm::DependenceAnalysisWrapperPass>(F).getDI();
#endif

            for_each_inst<llvm::LoadInst>(F, [&] (llvm::LoadInst *LI) {
                // get dominating stores
                std::vector<llvm::StoreInst *> SIs;
                llvm::dominators<llvm::StoreInst>(DT, LI, std::back_inserter(SIs));
                
                for (llvm::StoreInst *SI : SIs) {
                    const auto dep = DI.depends(SI, LI, true);
                    if (dep && dep->isFlow() && dep->isConsistent()) {
                        [[maybe_unused]] const auto res = rfs.emplace(LI, SI);
                        assert(res.second || DT.dominates(SI, rfs[LI]));
                    }
                }
            });
        });
                          
        
        // 2. initialize return value influencers
        std::set<llvm::Value *> ret_vals;
        for_each_inst<llvm::ReturnInst>(Fs.begin(), Fs.end(), [&ret_vals] (llvm::ReturnInst *RI) {
            if (llvm::Value *RV = RI->getReturnValue()) {
                ret_vals.insert(RV);
            }
        });
        
        // 3. propogate return values
        bool changed;
        const auto ret_vals_insert = [&] (llvm::Value *V) {
            changed |= ret_vals.insert(V).second;
            if (llvm::Argument *A = llvm::dyn_cast<llvm::Argument>(V)) {
                summaries[A->getParent()].ret_args.insert(A);
            }
        };
        do {
            changed = false;
            
            for (llvm::Value *V : ret_vals) {
                if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
                    if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
                        // propogate back from rf edge
                        const auto rf_it = rfs.find(LI);
                        if (rf_it != rfs.end()) {
                            llvm::StoreInst *SI = rf_it->second;
                            ret_vals_insert(SI->getValueOperand());
                        }
                    } else if (llvm::CallBase *CI = llvm::dyn_cast<llvm::CallBase>(I)) {
                        // propogate to arguments
                        if (llvm::Function *called_F = CI->getCalledFunction()) {
                            if (!called_F->isDeclaration()) {
                                for (llvm::Argument& called_A : called_F->args()) {
                                    if (summaries[called_F].ret_args.contains(&called_A)) {
                                        ret_vals_insert(CI->getArgOperand(called_A.getArgNo()));
                                    }
                                }
                            }
                        }
                    } else if (llvm::isa<llvm::BinaryOperator, llvm::GetElementPtrInst, llvm::PHINode, llvm::SelectInst, llvm::CastInst, llvm::CmpInst>(I)) {
                        // propogate to input operands
                        for (llvm::Value *op_V : I->operands()) {
                            ret_vals_insert(op_V);
                        }
                    } else if (llvm::isa<llvm::AllocaInst, llvm::BranchInst, llvm::FenceInst, llvm::StoreInst, llvm::SwitchInst, llvm::UnreachableInst>(I)) {
                        // ignore
                    } else {
                        std::string s;
                        llvm::raw_string_ostream os (s);
                        os << "clou: internal error: unhandled instruction: " << *I;
                        throw std::runtime_error(s);
                    }
                }
            }
            
        } while (changed);
        
        // 4. initialize public values with true transmitter operands
        std::set<llvm::Value *> pub_vals;
        const auto pub_vals_insert = [&] (llvm::Value *V) {
             changed |= pub_vals.insert(V).second;
            if (llvm::Argument *A = llvm::dyn_cast<llvm::Argument>(V)) {
                summaries[A->getParent()].pub_args.insert(A);
            }
        };
        
        for_each_inst<llvm::Instruction>(Fs.begin(), Fs.end(), [&] (llvm::Instruction *I) {
            llvm::errs() << "for inst " << *I << "\n";
            for (const TransmitterOperand& op : get_transmitter_sensitive_operands(I)) {
                if (op.kind == TransmitterOperand::TRUE) {
                    llvm::errs() << "true transmitter operand: " << *op.V << "\n";
                    pub_vals_insert(op.V);
                }
            }
        });
        
        // 5. propogate public values
        do {
            changed = false;
            
            // update always-public arguments, in case it has changed in SCC
            for_each_inst<llvm::CallBase>(Fs.begin(), Fs.end(), [&] (llvm::CallBase *CI) {
                if (llvm::Function *called_F = CI->getCalledFunction()) {
                    if (!called_F->isDeclaration()) {
                        const auto& called_pub_args = summaries[called_F].pub_args;
                        for (llvm::Argument& A : called_F->args()) {
                            if (called_pub_args.contains(&A)) {
                                pub_vals_insert(CI->getArgOperand(A.getArgNo()));
                            }
                        }
                    }
                }
            });
            
            for (llvm::Value *V : pub_vals) {
                if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
                    if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
                        // propogate back from rf edge
                        const auto rf_it = rfs.find(LI);
                        if (rf_it != rfs.end()) {
                            llvm::StoreInst *SI = rf_it->second;
                            pub_vals_insert(SI->getValueOperand());
                        }
                    } else if (llvm::CallBase *CI = llvm::dyn_cast<llvm::CallBase>(I)) {
                        // propogate through to arguments via return value
                        if (llvm::Function *called_F = CI->getCalledFunction()) {
                            if (!called_F->isDeclaration()) {
                                const auto& called_ret_args = summaries[called_F].ret_args;
                                for (llvm::Argument& called_A : called_F->args()) {
                                    if (called_ret_args.contains(&called_A)) {
                                        pub_vals_insert(CI->getArgOperand(called_A.getArgNo()));
                                    }
                                }
                            }
                        }
                    } else if (llvm::isa<llvm::BinaryOperator, llvm::GetElementPtrInst, llvm::PHINode, llvm::SelectInst, llvm::CastInst, llvm::CmpInst>(I)) {
                        // propogate to input operands
                        for (llvm::Value *op_V : I->operands()) {
                            pub_vals_insert(op_V);
                        }
                    } else if (llvm::isa<llvm::AllocaInst, llvm::BranchInst>(I)) {
                        // ignore
                    } else {
                        std::string s;
                        llvm::raw_string_ostream os (s);
                        os << "clou: internal error: unhandled instruction: " << *I;
                        throw std::runtime_error(s);
                    }
                }
                
            }
        } while (changed);
    
        return false;
    }
    
    virtual void print(llvm::raw_ostream& os, const llvm::Module *M) const override {
        if (M) {
            for_each_func_def(*M, [&] (const llvm::Function& F) {
                std::set<const llvm::Argument *> flagged;
                const FunctionSummary& summary = summaries.at(const_cast<llvm::Function *>(&F));
                for (const llvm::Argument& A : F.args()) {
                    llvm::Type *T = A.getType();
#ifndef NDEBUG
                    if (!llvm::isa<llvm::IntegerType, llvm::PointerType, llvm::VectorType, llvm::ArrayType>(T)) {
                        std::string s;
                        llvm::raw_string_ostream os (s);
                        os << "internal clou error: unexpected argument type '" << *T << "' in function '" << F.getName() << "'";
                        throw std::runtime_error(s);
                    }
#endif
                    if (llvm::isa<llvm::IntegerType, llvm::VectorType, llvm::ArrayType>(T) && !summary.pub_args.contains(const_cast<llvm::Argument *>(&A))) {
                        flagged.insert(&A);
                    }
                }
                
                if (!flagged.empty()) {
                    os << F.getName() << ": ";
                    for (auto it = flagged.begin(); it != flagged.end(); ++it) {
                        const llvm::Argument *A = *it;
                        if (it != flagged.begin()) {
                            os << "; ";
                        }
                        if (A->hasName()) {
                            os << A->getName();
                        } else {
                            os << *A;
                        }
                        
                    }
                    os << "\n";
                }
            });
        }
    }
};

struct SecretParamAnalysisOld final: public llvm::ModulePass {
    static inline char ID = 0;
    SecretParamAnalysisOld(): llvm::ModulePass(ID) {}
    
    virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
        AU.addRequired<llvm::DependenceAnalysisWrapperPass>();
    }
    
    using VariableValue = std::set<llvm::Value *>;
    
    VariableValue pub_vars;

    virtual bool runOnModule(llvm::Module& M) override {
        /* do dataflow analysis
         * Locations are (llvm::CallGraphNode *, llvm::Instruction *) pairs.
         * Note that we can't assume that the parameters of external functions are secret, since we haven't deduced
         * anything about them yet.
         * The public value set should be shared since we're assuming once a public value, always a public value.
         *
         * Dataflow Analysis:
         * 1. Initialize public variable set by adding all true transmitter operands in all functions.
         * 2. Get set of 'must RF' edges.
         * 3. Main loop:
         *     - propogate loads to store operands
         *     - propogate instructions to operands
         */
        
        pub_vars.clear();
        
        // 1. initialize public variable set
        for_each_func_def(M, [&] (llvm::Function& F) {
            for_each_inst<llvm::Instruction>(F, [&] (llvm::Instruction *I) {
                for (const TransmitterOperand& op : get_transmitter_sensitive_operands(I)) {
                    if (llvm::isa<llvm::Argument, llvm::Instruction>(op.V)) {
                        pub_vars.insert(op.V);
                    }
                }
            });
        });
        
        // 2. initialize reverse rfs
        std::map<llvm::LoadInst *, std::set<llvm::StoreInst *>> rfs; // TODO: is it ok to consider *any* store?
        for_each_func_def(M, [&] (llvm::Function& F) {
            llvm::DependenceInfo& DI = getAnalysis<llvm::DependenceAnalysisWrapperPass>(F).getDI();
            for_each_inst<llvm::LoadInst>(F, [&] (llvm::LoadInst *LI) {
                for_each_inst<llvm::StoreInst>(F, [&] (llvm::StoreInst *SI) {
                    const auto dep = DI.depends(SI, LI, true);
                    if (dep && dep->isFlow() && dep->isConsistent()) {
                        rfs[LI].insert(SI);
                    }
                });
            });
        });
        
        bool changed;
        do {
            changed = false;
            
            const auto pub_vars_insert = [&] (llvm::Value *V) {
                changed |= pub_vars.insert(V).second;
            };
            
            for (llvm::Value *pub_V : pub_vars) {
                if (llvm::Instruction *pub_I = llvm::dyn_cast<llvm::Instruction>(pub_V)) {
                    if (llvm::LoadInst *pub_LI = llvm::dyn_cast<llvm::LoadInst>(pub_I)) {
                        // try to propogate backwards through RF
                        const auto it = rfs.find(pub_LI);
                        if (it != rfs.end()) {
                            for (llvm::StoreInst *SI : it->second) {
                                pub_vars_insert(SI->getValueOperand());
                            }
                        }
                    } else if (llvm::CallBase *pub_C = llvm::dyn_cast<llvm::CallBase>(pub_I)) {
                        // propogate to callee's return value(s)
                        llvm::Function *called_F = pub_C->getCalledFunction();
                        if (called_F && !called_F->isDeclaration()) {
                            for_each_inst<llvm::ReturnInst>(*called_F, [&] (llvm::ReturnInst *RI) {
                                pub_vars_insert(RI->getReturnValue());
                            });
                            
                            // propogate arguments
                            for (llvm::Argument& A : called_F->args()) {
                                if (pub_vars.contains(&A)) {
                                    pub_vars_insert(pub_C->getArgOperand(A.getArgNo()));
                                }
                            }
                        }
                        
                    } else if (llvm::isa<llvm::BinaryOperator, llvm::CmpInst, llvm::GetElementPtrInst, llvm::PHINode, llvm::SelectInst, llvm::UnaryInstruction, llvm::FreezeInst>(pub_I)) {
                        for (llvm::Value *op : pub_I->operands()) {
                            pub_vars_insert(op);
                        }
                    } else if (llvm::isa<llvm::BranchInst, llvm::FenceInst, llvm::ReturnInst, llvm::StoreInst, llvm::UnreachableInst>(pub_I)) {
                        // do nothing
                    } else if (llvm::isa<llvm::InsertValueInst, llvm::InsertElementInst>(pub_I)) {
                        // we could technically model these to gain more precision, but aren't considering them for now
                    } else {
                        std::string s;
                        llvm::raw_string_ostream os (s);
                        os << "clou internal error: unhandled instruction " << *pub_I << "\n";
                        throw std::runtime_error(s);
                    }
                }
            }
            
        } while (changed);
        
        return false;
    }
    
    virtual void print(llvm::raw_ostream& os, const llvm::Module *M) const override {
        if (M) {
            for_each_func_def(*M, [&] (const llvm::Function& F) {
                std::set<const llvm::Argument *> flagged;
                for (const llvm::Argument& A : F.args()) {
                    llvm::Type *T = A.getType();
#ifndef NDEBUG
                    if (!llvm::isa<llvm::IntegerType, llvm::PointerType, llvm::VectorType, llvm::ArrayType>(T)) {
                        std::string s;
                        llvm::raw_string_ostream os (s);
                        os << "internal clou error: unexpected argument type '" << *T << "' in function '" << F.getName() << "'";
                        throw std::runtime_error(s);
                    }
#endif
                    if (llvm::isa<llvm::IntegerType, llvm::VectorType, llvm::ArrayType>(T) && !pub_vars.contains(const_cast<llvm::Argument *>(&A))) {
                        flagged.insert(&A);
                    }
                }
                if (!flagged.empty()) {
                    os << F.getName() << ": ";
                    for (auto it = flagged.begin(); it != flagged.end(); ++it) {
                        const llvm::Argument *A = *it;
                        if (it != flagged.begin()) {
                            os << "; ";
                        }
                        if (A->hasName()) {
                            os << A->getName();
                        } else {
                            os << *A;
                        }
                        
                    }
                    os << "\n";
                }
            });
        }
    }
    
};

llvm::RegisterPass<SecretParamAnalysis> X {
    "secret-param-analysis", "Secret Parameter Analysis Pass"
};

}
