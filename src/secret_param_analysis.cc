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

#include <map>
#include <set>
#include <memory>

#include "transmitter.h"

namespace {

struct SecretParamAnalysis final: public llvm::ModulePass {
    static inline char ID = 0;
    SecretParamAnalysis(): llvm::ModulePass(ID) {}
    
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
