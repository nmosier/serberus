#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include "clou/util.h"

namespace clou {
  namespace {

    struct DuplicatePass final : public llvm::ModulePass {
      static inline char ID = 0;
      DuplicatePass(): ModulePass(ID) {}

      bool canDuplicate(llvm::Function::LinkageTypes l) {
	using L = llvm::Function::LinkageTypes;
	switch (l) {
	case L::ExternalLinkage: return true;
	case L::AvailableExternallyLinkage: return true;
	case L::LinkOnceAnyLinkage: return true;
	case L::LinkOnceODRLinkage: return true;
	case L::WeakAnyLinkage: return false;
	case L::WeakODRLinkage: return false;
	case L::ExternalWeakLinkage: return false;
	case L::InternalLinkage: return true;
	case L::PrivateLinkage: return true;
	default: llvm_unreachable("Unexpected linkage type for function");
	}
      }

      bool shouldDuplicate(const llvm::Function& F) {
	if (F.isDeclaration())
	  return false;
	if (!canDuplicate(F.getLinkage()))
	  return false;
	if (util::functionIsDirectCallOnly(F))
	  return false;	  

	// Check if it has a direct call
	const bool has_direct_call = llvm::any_of(F.uses(), [] (const llvm::Use& U) -> bool {
	  if (const auto *C = llvm::dyn_cast<llvm::CallBase>(U.getUser()))
	    return U == C->getCalledOperandUse();
	  return false;
	});

	if (!has_direct_call)
	  return false;

	return true;
      }

      static inline const char *suffix = ".llsct.dup";

      static void cloneFunction(llvm::Function& F) {
	assert(!F.getName().endswith(suffix) && "Infinite cloning loop!");
	llvm::errs() << "Duplicating " << F.getName() << "\n";
	llvm::ValueToValueMapTy VMap;
	llvm::Function *NewF = llvm::CloneFunction(&F, VMap, nullptr);
	const llvm::Twine NewName = F.getName() + suffix;
	NewF->setName(NewName);
	NewF->setLinkage(llvm::Function::InternalLinkage);

	// Replace all direct calls to F with NewF.
	for (const llvm::Use& U : F.uses()) {
	  if (auto *C = llvm::dyn_cast<llvm::CallBase>(U.getUser())) {
	    if (U.getOperandNo() == 0) {
	      C->setOperand(U.getOperandNo(), NewF);
	    }
	  }
	}
      }

      bool runOnModule(llvm::Module& M) override {
	// FIXME: Should this really be a SCC pass? I think the current implementation is still correct, but may required 2x more work.
	std::vector<llvm::Function *> worklist;
	for (llvm::Function& F : M)
	  if (shouldDuplicate(F))
	    worklist.push_back(&F);
	for (llvm::Function *F : worklist)
	  cloneFunction(*F);
	return !worklist.empty();
      }
    };

    const llvm::RegisterPass<DuplicatePass> X {"llsct-duplicate-pass", "LLSCT's Duplicate Pass"};
    const util::RegisterClangPass<DuplicatePass> Y;
    
  }
}
