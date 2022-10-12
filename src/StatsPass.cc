#include <string>
#include <fstream>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Clou/Clou.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

#include "clou/util.h"

namespace clou {
  namespace {
    struct StatisticsPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      StatisticsPass(): llvm::FunctionPass(ID) {}

      struct AccessStats {
	bool stats;
      };

      template <class Range>
      static AddrStat getAddrStat(const Range& rng) {
	AddrStat st;
	for (auto& I : rng) {
	  llvm::Value *V = I.getPointerOperand();
	  if (util::isConstantAddress(V)) {
	    st.ca++;
	  } else {
	    st.nca++;
	  }
	}
	return st;
      }

      bool runOnFunction(llvm::Function& F) override {
	if (!ClouLog)
	  return false;
	
	const auto addr_loads = getAddrStat(util::instructions<llvm::LoadInst>(F));
	const auto addr_stores = getAddrStat(util::instructions<llvm::StoreInst>(F));

	std::string logpath;
	{
	  llvm::raw_string_ostream ss(logpath);
	  ss << ClouLogDir << "/" << F.getName() << ".stats";
	}
	std::ofstream os (logpath);
	os << "ca_loads: " << addr_loads.ca << "\n";
	os << "nca_loads: " << addr_loads.nca << "\n";
	os << "ca_stores: " << addr_stores.ca << "\n";
	os << "nca_stores: " << addr_stores.nca << "\n";

	return false;
      }
    };

    llvm::RegisterPass<StatisticsPass> X {"clou-stats", "LLVM-SCT Staistics Pass"};
    util::RegisterClangPass<StatisticsPass> Y;
  }
}
