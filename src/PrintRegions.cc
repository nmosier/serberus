#include <llvm/Analysis/RegionPass.h>
#include <llvm/Analysis/RegionInfo.h>
#include <llvm/Pass.h>

namespace clou {
  namespace {

    struct PrintRegions final : public llvm::RegionPass {
      static inline char ID = 0;
      PrintRegions(): llvm::RegionPass(ID) {}

      bool runOnRegion(llvm::Region *R, llvm::RGPassManager& RGM) override {
	const llvm::BasicBlock* entry = R->getEntry();
	const llvm::BasicBlock* exit = R->getExit();
	const llvm::BasicBlock *entering = R->getEnteringBlock();
	llvm::SmallVector<llvm::BasicBlock *, 4> exitings;
	R->getExitingBlocks(exitings);

	auto& os = llvm::errs();
	
	os << "\n========================================================\n";
	os << "Entry: ";
	if (entry) { os << *entry; } else { os << "(none)"; }
	os << "\n";
	os << "Exit: ";
	if (exit) { os << *exit; } else { os << "(none)"; }
	os << "\n";
	os << "Entering: ";
	if (entering) {
	  llvm::errs() << *entering;
	} else {
	  llvm::errs() << "(none)";
	}
	llvm::errs() << "\n";
	llvm::errs() << "Exiting (" << exitings.size() << "):\n";
	for (const llvm::BasicBlock *B : exitings) {
	  llvm::errs() << *B << "\n";
	}
	os << "\n";
	os << "Blocks:\n";
	for (const llvm::BasicBlock *B : R->blocks()) {
	  os << "\n" << *B << "\n";
	}
	os << "\n========================================================\n";

	return false;
      }
    };

    llvm::RegisterPass<PrintRegions> X {"print-regions", "Print Regions"};
    
  }
}
