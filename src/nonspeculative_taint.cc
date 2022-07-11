#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>

#include "nonspeculative_taint.h"
#include "util.h"
#include "transmitter.h"

char NonspeculativeTaint::ID = 0;

NonspeculativeTaint::NonspeculativeTaint(): MySCCPass(ID) {}

void NonspeculativeTaint::getAnalysisUsage(llvm::AnalysisUsage& AU) const {
    AU.addRequired<llvm::AAResultsWrapperPass>();
    AU.setPreservesAll();
}

bool NonspeculativeTaint::runOnSCC(const MySCC& SCC) {
    std::set<llvm::Function *> Fs;
    std::transform(SCC.begin(), SCC.end(), std::inserter(Fs, Fs.end()), [] (llvm::CallGraphNode *CGN) {
        return CGN->getFunction();
    });
    
    /* Propogate public values.
     * This time we can assume a lot more than in the 'secret param analysis'.
     * We'll first propogate public return values through to arguments.
     */
    
    // 1. Map loads to store sets (not true rfs).
    std::map<llvm::LoadInst *, std::set<llvm::StoreInst *>> rfs;
    for_each_func_def(Fs.begin(), Fs.end(), [&] (llvm::Function& F) {
        llvm::AAResults& AA = getAnalysis<llvm::AAResultsWrapperPass>(F).getAAResults();
        for_each_inst<llvm::LoadInst>(F, [&] (llvm::LoadInst *LI) {
            auto& rf = rfs[LI];
            for_each_inst<llvm::StoreInst>(F, [&] (llvm::StoreInst *SI) {
                if (AA.alias(LI->getPointerOperand(), SI->getPointerOperand()) == llvm::AliasResult::MustAlias) {
                    rf.insert(SI);
                }
            });
        });
    });
    
    // 2. Initialize return value influencers
    for_each_inst<llvm::ReturnInst>(Fs.begin(), Fs.end(), [&] (llvm::ReturnInst *RI) {
        if (llvm::Value *RV = RI->getReturnValue()) {
            ret_vals.insert(RV);
        }
    });
    
    // 3. Propogate return values
    bool changed;
    const auto ret_vals_insert = [&] (llvm::Value *V) {
        changed |= ret_vals.insert(V).second;
    };
    do {
        changed = false;
        
        for (llvm::Value *V : ret_vals) {
            if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
                if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
                    // propogate back from rf edges
                    for (llvm::StoreInst *SI : rfs[LI]) {
                        ret_vals_insert(SI->getValueOperand());
                    }
                } else if (llvm::CallBase *CI = llvm::dyn_cast<llvm::CallBase>(I)) {
                    // propogate to arguments
                    if (llvm::Function *called_F = CI->getCalledFunction()) {
                        if (!called_F->isDeclaration()) {
                            for (llvm::Argument& called_A : called_F->args()) {
                                if (ret_vals.contains(&called_A)) {
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
                } else if (llvm::isa<llvm::ExtractValueInst, llvm::ExtractElementInst>(I)) {
                    // TODO: ignore for now, but can consider for more precision later on
                } else {
                    unhandled_instruction(*I);
                }
            }
        }
    } while (changed);
    
    // 4. Initialize public values with (a) true transmitter operands, (b) call arguments, (c) public annotations.
    // 4(a) true transmitter operands
    for_each_inst<llvm::Instruction>(Fs.begin(), Fs.end(), [&] (llvm::Instruction *I) {
        for (const TransmitterOperand& op : get_transmitter_sensitive_operands(I)) {
            if (op.kind == TransmitterOperand::TRUE) {
                pub_vals.insert(op.V);
            }
        }
    });
    
    // 4(b) call arguments
    for_each_inst<llvm::CallBase>(Fs.begin(), Fs.end(), [&] (llvm::CallBase *CI) {
        for (llvm::Value *arg_V : CI->args()) {
            pub_vals.insert(arg_V);
        }
    });
    
    // 4(c) public annotations
    for_each_inst<llvm::IntrinsicInst>(Fs.begin(), Fs.end(), [&] (llvm::IntrinsicInst *II) {
        if (II->getIntrinsicID() == llvm::Intrinsic::annotation) {
            // TODO: double-check how LLVM does this
            // TODO: extract this into function
            llvm::Value *V = II->getArgOperand(1);
            auto *GEP = llvm::cast<llvm::ConcreteOperator<llvm::Operator, llvm::Instruction::GetElementPtr>>(V);
            llvm::Value *V_ = GEP->getOperand(0);
            llvm::GlobalVariable *GV = llvm::cast<llvm::GlobalVariable>(V_);
            const auto str = llvm::cast<llvm::ConstantDataArray>(GV->getInitializer())->getAsCString();
            if (str == "public") {
                pub_vals.insert(II->getArgOperand(0));
            }
        }
    });
    
    // 5. Propogate public values
    const auto pub_vals_insert = [&] (llvm::Value *V) {
        changed |= pub_vals.insert(V).second;
    };
    do {
        changed = false;
        
        // update always-public arguments
        for_each_inst<llvm::CallBase>(Fs.begin(), Fs.end(), [&] (llvm::CallBase *CI) {
            for (llvm::Value *arg_V : CI->args()) {
                pub_vals_insert(arg_V);
            }
        });
        
        // update always-public arguments, in case it has changed in SCC
        for_each_inst<llvm::CallBase>(Fs.begin(), Fs.end(), [&] (llvm::CallBase *CI) {
            if (llvm::Function *called_F = CI->getCalledFunction()) {
                if (!called_F->isDeclaration()) {
                    for (llvm::Argument& A : called_F->args()) {
                        if (pub_vals.contains(&A)) {
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
                    for (llvm::StoreInst *SI : rfs[LI]) {
                        pub_vals_insert(SI->getValueOperand());
                    }
                } else if (llvm::CallBase *CI = llvm::dyn_cast<llvm::CallBase>(I)) {
                    // propogate through to arguments via return value
                    if (llvm::Function *called_F = CI->getCalledFunction()) {
                        if (!called_F->isDeclaration()) {
                            for (llvm::Argument& called_A : called_F->args()) {
                                if (ret_vals.contains(&called_A)) {
                                    pub_vals_insert(CI->getArgOperand(called_A.getArgNo()));
                                }
                            }
                        }
                    }
                } else if (llvm::isa<llvm::BinaryOperator, llvm::GetElementPtrInst, llvm::PHINode, llvm::SelectInst, llvm::CastInst, llvm::CmpInst, llvm::FreezeInst>(I)) {
                    // propogate to input operands
                    for (llvm::Value *op_V : I->operands()) {
                        pub_vals_insert(op_V);
                    }
                } else if (llvm::isa<llvm::AllocaInst, llvm::BranchInst>(I)) {
                    // ignore
                } else if (llvm::isa<llvm::ExtractValueInst, llvm::InsertElementInst, llvm::InsertValueInst>(I)) {
                    // TODO: ignored, handle later for higher precision
                } else {
                    unhandled_instruction(*I);
                }
            }
            
        }
        
    } while (changed);
    
    return false;
}

void NonspeculativeTaint::print(llvm::raw_ostream& os, const llvm::Module *M) const {
    if (M) {
        for (const llvm::Function& F : *M) {
            os << F.getName() << ":\n";
            for (const llvm::Argument& A : F.args()) {
                if (!pub_vals.contains(const_cast<llvm::Argument *>(&A))) {
                    os << A << "\n";
                }
            }
            for_each_inst<llvm::Instruction>(F, [&] (const llvm::Instruction *I) {
                if (!pub_vals.contains(const_cast<llvm::Instruction *>(I))) {
                    if (!I->getType()->isVoidTy()) {
                        os << *I << "\n";
                    }
                }
            });
        }
    }
}

bool NonspeculativeTaint::secret(const llvm::Value *V) const {
    llvm::Value *V_ = const_cast<llvm::Value *>(V);
    if (const llvm::Argument *A = llvm::dyn_cast<llvm::Argument>(V)) {
        return !pub_vals.contains(V_);
    } else if (const llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
        if (!I->getType()->isVoidTy()) {
            return !pub_vals.contains(V_);
        }
    }
    return false;
}

namespace {

llvm::RegisterPass<NonspeculativeTaint> X {
    "nonspeculative-taint", "Nonspeculative Taint Pass"
};

}
