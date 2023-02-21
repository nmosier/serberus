#include "clou/analysis/ConstantAddressAnalysis.h"

#include "clou/util.h"

namespace clou {

  void ConstantAddressAnalysis::getAnalysisUsage(llvm::AnalysisUsage& AU) const {
    AU.setPreservesAll();
  }

  bool ConstantAddressAnalysis::runOnModule(llvm::Module& M) {
    ca_args.clear();
    auto& map = ca_args;
    std::map<const llvm::Function *, ArgumentSet> bak;

    // Get list of direct-call-only functions (these are the only ones we can make assumptions about).
    std::vector<const llvm::Function *> functions;
    for (const llvm::Function& F : M) {
      if (util::functionIsDirectCallOnly(F)) {
	functions.push_back(&F);
      }
    }

    // Initialize map to top (assume all arguments are constant-address in the beginning).
    for (const llvm::Function *F : functions) {
      ArgumentSet& args = map[F];
      for (const llvm::Argument& arg : F->args())
	if (arg.getType()->isPointerTy())
	  args.insert(&arg);
    }

    // Run data-flow analysis until fixpoint.
    do {
      bak = map;

      for (const llvm::Function *Callee : functions) {
	ArgumentSet& args = map.at(Callee);
	
	// Iterate over callers of callee.
	for (const llvm::User *User : Callee->users()) {
	  const llvm::CallBase *I = llvm::cast<llvm::CallBase>(User);

	  // Try to prove that each argument is non-constant-address.
	  for (auto it = args.begin(); it != args.end(); ) {
	    const llvm::Argument *A = *it;
	    const unsigned ArgNo = A->getArgNo();
	    const bool is_nca = ArgNo >= I->arg_size() || !isConstantAddress(I->getArgOperand(ArgNo));
	    if (is_nca) {
	      if (Callee->getName() == "Hacl_Impl_Curve25519_Field51_fmul") {
		llvm::errs() << "Marking argument as NCA:\n"
			     << "Formal: " << *A << "\n"
			     << "Actual: " << *I->getArgOperand(ArgNo) << "\n"
			     << "Call:   " << *I << "\n"
			     << "Caller: " << I->getParent()->getParent()->getName() << "\n"
			     << "Filename: " << Callee->getParent()->getSourceFileName() << "\n";
	      }
	      it = args.erase(it); 
	    } else {
	      ++it;
	    }
	  }
	  
	}
      }
      
    } while (map != bak);

    return false;
  }

  bool ConstantAddressAnalysis::isConstantAddress(const llvm::Value *V) const {
    assert(V->getType()->isPointerTy());
    if (const llvm::Argument *A = llvm::dyn_cast<llvm::Argument>(V)) {
      const auto it = ca_args.find(A->getParent());
      if (it == ca_args.end())
	return false;
      else
	return it->second.contains(A);
    } else if (llvm::isa<llvm::PHINode, llvm::CallBase, llvm::LoadInst, llvm::IntToPtrInst>(V)) {
      return false;
    } else if (llvm::isa<llvm::Constant, llvm::AllocaInst>(V)) {
      return true;
    } else if (const auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(V)) {
      return GEP->hasAllConstantIndices() && isConstantAddress(GEP->getPointerOperand());
    } else if (const auto *BC = llvm::dyn_cast<llvm::BitCastInst>(V)) {
      if (BC->getSrcTy()->isPointerTy())
	return isConstantAddress(BC->getOperand(0));
      else
	return false;
    } else if (const auto *Select = llvm::dyn_cast<llvm::SelectInst>(V)) {
      return isConstantAddress(Select->getTrueValue()) && isConstantAddress(Select->getFalseValue());
    } else {
      unhandled_value(*V);
    }
  }

  static llvm::RegisterPass<ConstantAddressAnalysis> X {"constant-address-analysis", "LLSCT's Constant Address Analysis", false, true};

}
