#include <set>

#include <llvm/Pass.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/MemoryDependenceAnalysis.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constant.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/WithColor.h>

#include "clou/util.h"
#include "clou/Transmitter.h"

namespace clou {
  namespace {

    struct ContractChecker final : public llvm::FunctionPass {
      static inline char ID = 0;
      ContractChecker(): llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<llvm::AAResultsWrapperPass>();
	AU.addRequired<llvm::MemoryDependenceWrapperPass>();
	AU.setPreservesAll();
      }

      bool annotatedPublicReturnValue(llvm::Function& F) const {
	auto *M = F.getParent();
	if (auto *GV = M->getGlobalVariable("llvm.global.annotations")) {
	  for (llvm::Value *AOp : GV->operands()) {
	    if (auto *CA = llvm::dyn_cast<llvm::ConstantArray>(AOp)) {
	      for (llvm::Value *CAOp : CA->operands()) {
		if (auto *CS = llvm::dyn_cast<llvm::ConstantStruct>(CAOp)) {
		  if (CS->getNumOperands() >= 2) {
		    if (&F == llvm::cast<llvm::Function>(CS->getOperand(0)->getOperand(0))) {
		      if (auto *GAnn = llvm::dyn_cast<llvm::GlobalVariable>(CS->getOperand(1)->getOperand(0))) {
			if (auto *A = llvm::dyn_cast<llvm::ConstantDataArray>(GAnn->getOperand(0))) {
			  const auto AS = A->getAsCString();
			  if (AS == "public") {
			    return true;
			  }
			}
		      }
		    }
		  }
		}
	      }
	    }
	  }
	}
	return false;
      }

      bool runOnFunction(llvm::Function& F) override {
	std::set<llvm::Value *> mem, mem_bak; // public memory
	std::set<llvm::Value *> vals, vals_bak; // public values

	auto& MD = getAnalysis<llvm::MemoryDependenceWrapperPass>().getMemDep();

	const auto is_pub = [&vals] (llvm::Value *V) -> bool {
	  return vals.contains(V) || llvm::isa<llvm::Constant>(V);
	};
	const auto all_pub = [&is_pub] (const auto& range) -> bool {
	  return llvm::all_of(range, is_pub);
	};

	for (llvm::Argument& A : F.args()) {
	  if (A.getType()->isPointerTy()) {
	    vals.insert(&A);
	  }
	}

	for (llvm::IntrinsicInst& II : util::instructions<llvm::IntrinsicInst>(F)) {
	  if (II.getIntrinsicID() == llvm::Intrinsic::annotation) {
	    // the second argument is the annotation string
	    if (const auto *str_GV = llvm::dyn_cast<llvm::GlobalVariable>(II.getArgOperand(1)->stripPointerCasts())) {
	      if (const auto *str_V = llvm::dyn_cast<llvm::ConstantDataArray>(str_GV->getInitializer())) {
		if (str_V->isCString()) {
		  const llvm::StringRef str = str_V->getAsCString();
		  if (str == "public") {
		    vals.insert(II.getArgOperand(0));
		  }
		}
	      }
	    }
	  }
	}

	if (annotatedPublicReturnValue(F)) {
	  for (llvm::ReturnInst& RI : util::instructions<llvm::ReturnInst>(F)) {
	    if (auto *RV = RI.getReturnValue()) {
	      vals.insert(RV);
	    }
	  }
	}
      
	do {
	  mem_bak = mem;
	  vals_bak = vals;	  

	  for (llvm::Instruction& I : llvm::instructions(F)) {
	    // Pointers are public per CT
	    if (I.getType()->isPointerTy()) {
	      vals.insert(&I);
	    }

	    // Sensitive transmitter operands are public per CT
	    for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I)) {
	      if (op.kind == TransmitterOperand::TRUE) {
		vals.insert(op.V);
	      }
	    }

	    // Assume that called functions obey the contract
	    if (auto *C = llvm::dyn_cast<util::ExtCallBase>(&I)) {
	      for (llvm::Value *V : C->args()) {
		vals.insert(V);
	      }
	      if (!C->getType()->isVoidTy()) {
		vals.insert(C);
	      }
	    }

	    // TODO: Merge these for simpler code.

	    // Backwards publicity propagation
	    if (vals.contains(&I) && I.getNumOperands() > 0) {
	      if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
		mem.insert(LI->getPointerOperand());
	      } else if (llvm::isa<llvm::CmpInst, llvm::GetElementPtrInst, llvm::BinaryOperator, llvm::PHINode, llvm::CastInst,
			 llvm::SelectInst, llvm::FreezeInst, llvm::AllocaInst>(I)) {
		// All input operands must be public as well
		for (llvm::Value *op : I.operands()) {
		  vals.insert(op);
		}
	      } else if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
		switch (II->getIntrinsicID()) {
		case llvm::Intrinsic::vector_reduce_and:
		case llvm::Intrinsic::vector_reduce_add:		
		case llvm::Intrinsic::vector_reduce_or:
		case llvm::Intrinsic::fshl:
		case llvm::Intrinsic::ctpop:
		case llvm::Intrinsic::x86_aesni_aeskeygenassist:
		case llvm::Intrinsic::x86_aesni_aesenc:
		case llvm::Intrinsic::x86_aesni_aesenclast:
		case llvm::Intrinsic::bswap:
		case llvm::Intrinsic::x86_pclmulqdq:
		case llvm::Intrinsic::umin:
		case llvm::Intrinsic::umax:
		case llvm::Intrinsic::x86_rdrand_32:
		  for (llvm::Value *V : II->args()) {
		    vals.insert(V);
		  }
		  break;
		case llvm::Intrinsic::annotation:
		  vals.insert(II->getArgOperand(0));
		  break;
		default:
		  warn_unhandled_intrinsic(II);
		}
	      } else if (llvm::isa<util::ExtCallBase, llvm::ExtractValueInst, llvm::ShuffleVectorInst, llvm::ExtractElementInst,
			 llvm::InsertElementInst>(&I)) {
		// conservatively don't propagate
	      } else {
		unhandled_instruction(I);
	      }
	    }

	    // Forwards publicity propagation
	    if (!I.getType()->isVoidTy() && I.getNumOperands() > 0 && !vals.contains(&I)) {
	      if (llvm::isa<llvm::LoadInst>(&I)) {
		// ignore
	      } else if (llvm::isa<llvm::CmpInst, llvm::BinaryOperator, llvm::PHINode, llvm::CastInst, llvm::SelectInst, llvm::FreezeInst>(I)) {
		if (all_pub(I.operands())) {
		  vals.insert(&I);
		}
	      } else if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
		switch (II->getIntrinsicID()) {
		case llvm::Intrinsic::vector_reduce_and:
		case llvm::Intrinsic::vector_reduce_add:		
		case llvm::Intrinsic::vector_reduce_or:
		case llvm::Intrinsic::fshl:
		case llvm::Intrinsic::ctpop:
		case llvm::Intrinsic::x86_aesni_aeskeygenassist:
		case llvm::Intrinsic::x86_aesni_aesenc:
		case llvm::Intrinsic::x86_aesni_aesenclast:
		case llvm::Intrinsic::bswap:
		case llvm::Intrinsic::x86_pclmulqdq:
		case llvm::Intrinsic::umin:
		case llvm::Intrinsic::umax:
		case llvm::Intrinsic::x86_rdrand_32:
		  if (all_pub(II->args())) {
		    vals.insert(II);
		  }
		  break;
		case llvm::Intrinsic::annotation:
		  if (is_pub(II->getArgOperand(0))) {
		    vals.insert(II);
		  }
		  break;
		default:
		  warn_unhandled_intrinsic(II);
		}
	      } else if (llvm::isa<util::ExtCallBase, llvm::ExtractValueInst, llvm::InsertElementInst, llvm::ExtractElementInst, llvm::ShuffleVectorInst>(&I)) {
		// conservatively don't propagate
	      } else {
		unhandled_instruction(I);
	      }
	    }

	    // Conservative Publicity propagation through memory
	    if (llvm::isa<llvm::LoadInst>(&I)) {
	      const auto MDR = MD.getDependency(&I);
	      // TODO: handle non-local dependencies.
	      // For an example, see llvm-project/llvm/Analysis/MemDepPrinter.cpp
	      if (!MDR.isNonLocal()) {
		if (MDR.isDef()) {
		  llvm::Instruction *store_I = MDR.getInst();
		  if (auto *store_SI = llvm::dyn_cast<llvm::StoreInst>(store_I)) {
		    // If stored value is public, then load is public
		    llvm::Value *store_value = store_SI->getValueOperand();
		    if (is_pub(store_value)) {
		      vals.insert(&I);
		    }
		    // If loaded value is public, then stored value is public
		    if (is_pub(&I)) {
		      vals.insert(store_value);
		    }
		  } else {
		    unhandled_instruction(*store_I);
		  }
		}
	      }
	    }
	    
	  }

	} while (!(mem == mem_bak && vals == vals_bak));

	// TODO: check

	bool ok = true;

	// Check for non-public arguments.
	for (llvm::Argument& A : F.args()) {
	  if (!vals.contains(&A)) {
	    std::string name = A.getName().str();
	    if (name.empty()) {
	      name = std::to_string(A.getArgNo());
	    }

	    std::string prefix;
	    {
	      llvm::raw_string_ostream os(prefix);
	      if (const auto *SP = F.getSubprogram()) {
		os << SP->getDirectory() << "/" << SP->getFilename() << ":" << SP->getLine();
	      }
	    }

	    auto& os = llvm::WithColor::error(llvm::errs(), prefix);
	    os << "argument '" << name << "' possibly secret in function " << F.getName() << "\n";

	    ok = false;
	  }
	}

	// Check for non-public return values.
	for (llvm::ReturnInst& RI : util::instructions<llvm::ReturnInst>(F)) {
	  if (auto *RV = RI.getReturnValue()) {
	    if (!is_pub(RV)) {
	      std::string prefix;
	      {
		llvm::raw_string_ostream os(prefix);
		if (const auto& DL = RI.getDebugLoc()) {
		  if (const auto *scope = llvm::cast<llvm::DIScope>(DL.getScope())) {
		    os << scope->getDirectory() << "/" << scope->getFilename() << ":" << DL.getLine() << ":" << DL.getCol();
		  }
		}
	      }

	      auto& os = llvm::WithColor::error(llvm::errs(), prefix);
	      os << "return value possibly secret in function " << F.getName() << "\n";
#if 0	      
	      os << RI << "\n" << *RV << "\n";
	      os << F << "\n";
#endif
	      ok = false;
	    }
	  }
	}

	if (!ok)
	  std::_Exit(EXIT_FAILURE);

	
	return false;
      }
    };

    llvm::RegisterPass<ContractChecker> X {"sct-check-contract", "LLSCT's Contract Checker"};
    util::RegisterClangPass<ContractChecker> Y;
    
  }
}
