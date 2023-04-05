#include <map>
#include <set>
#include <stack>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Attributes.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/Analysis/CallGraphSCCPass.h>

#include "clou/util.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/analysis/LeakAnalysis.h"
#include "clou/containers.h"
#include "clou/analysis/NonspeculativeTaintAnalysis.h"
#include "clou/analysis/ConstantAddressAnalysis.h"
#include "clou/Mitigation.h"

namespace clou {
  namespace {

#if 1
    struct InlinePass final : public llvm::FunctionPass {
      static inline char ID = 0;
      InlinePass(): llvm::FunctionPass(ID) {}

      using CBSet = std::set<llvm::CallBase *>;

      static inline constexpr unsigned inline_limit = 100; // per-function inline limit
      std::map<const llvm::Function *, unsigned> inline_counts; // inline counts per function

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<ConstantAddressAnalysis>();
	AU.addRequired<NonspeculativeTaint>();
	AU.addRequired<SpeculativeTaint>();
	AU.addRequired<LeakAnalysis>();
      }

      std::set<llvm::StoreInst *> compute_nca_stores(llvm::Function& F) {
	auto& CAA = getAnalysis<ConstantAddressAnalysis>();
	auto& NST = getAnalysis<NonspeculativeTaint>();
	auto& ST = getAnalysis<SpeculativeTaint>();
	std::set<llvm::StoreInst *> stores;
	for (llvm::StoreInst& SI : util::instructions<llvm::StoreInst>(F)) {
	  llvm::Value *PtrOp = SI.getPointerOperand();
	  llvm::Value *ValOp = SI.getValueOperand();
	  if (!CAA.isConstantAddress(PtrOp) && (NST.secret(ValOp) || ST.secret(ValOp)))
	    stores.insert(&SI);
	}
	return stores;
      }      

      bool calleeWouldBenefitFromInlining(llvm::Function& F) {
	auto& LA = getAnalysis<LeakAnalysis>();
	auto& NST = getAnalysis<NonspeculativeTaint>();
	auto& ST = getAnalysis<SpeculativeTaint>();
	
	// Check if there's a NCAS-RET.
	std::stack<llvm::Instruction *> todo;
	std::set<llvm::Instruction *> seen;
	for (llvm::StoreInst *SI : compute_nca_stores(F))
	  todo.push(SI);
	while (!todo.empty()) {
	  auto *I = todo.top();
	  todo.pop();
	  if (!seen.insert(I).second)
	    continue;

	  if (llvm::isa<llvm::ReturnInst>(I))
	    return true;

	  if (LA.mayLeak(I) && (NST.secret(I) || ST.secret(I)))
	    continue;

	  if (auto *C = llvm::dyn_cast<llvm::CallBase>(I))
	    if (util::mayLowerToFunctionCall(*C))
	      continue;

	  for (auto *succ : llvm::successors_inst(I))
	    todo.push(succ);
	}
	return false;
      }

      llvm::CallBase *handleSecretStore(llvm::StoreInst *SI, const CBSet& skip) {
	// check if we encounter any public loads along the way
	auto& ST = getAnalysis<SpeculativeTaint>();
	auto& LA = getAnalysis<LeakAnalysis>();

	std::set<llvm::Instruction *> seen;
	std::stack<llvm::Instruction *> todo;
	todo.push(SI);
	while (!todo.empty()) {
	  llvm::Instruction *I = todo.top();
	  todo.pop();

	  if (!seen.insert(I).second)
	    continue;

	  // Handle if call.
	  if (auto *CB = llvm::dyn_cast<llvm::CallBase>(I)) {
	    if (util::mayLowerToFunctionCall(*CB)) {
	      if (const auto *CalledF = util::getCalledFunction(CB)) {
		if (CalledF->isDeclaration()) {
		  continue; // Stop exploring this path, since we have an external call.
		} else if (skip.contains(CB)) {
		  continue; // Stop exploring this path, since we have already been unable to handle this call.
		} else {
		  // Return this call as candidate for inlining.
		  return CB;
		}
	      } else {
		continue; // Stop exploring this path, since it looks like an indirect call.
	      }
	    }
	  }

	  // Handle loads.
	  if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
	    if (!ST.secret(LI) && LA.mayLeak(LI)) {
	      // stop exploring this path
	      continue;
	    }
	  }

	  // Continue exploring paths.
	  for (auto *succ : llvm::successors_inst(I)) {
	    todo.push(succ);
	  }
	}
	return nullptr;
      }

      llvm::CallBase *getCallToInline(llvm::Function& F, CBSet& skip) {
	auto& ST = getAnalysis<SpeculativeTaint>();
	auto& NST = getAnalysis<NonspeculativeTaint>();
	const auto& CAA = getAnalysis<ConstantAddressAnalysis>();

	for (llvm::Instruction& I : llvm::instructions(F)) {
	  if (llvm::isa<llvm::CallBase>(&I)) {
	    // ignore
	    // TODO: handle intrinsics properly
	  } else if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
	    llvm::Value *V = SI->getValueOperand();
	    if (!CAA.isConstantAddress(SI->getPointerOperand()) && (NST.secret(V) || ST.secret(V)))
	      if (llvm::CallBase *CB = handleSecretStore(SI, skip))
		return CB;
	  }
	}

	for (llvm::CallBase& C : util::instructions<llvm::CallBase>(F)) {
	  if (skip.contains(&C))
	    continue;
	  auto *F = util::getCalledFunction(&C);
	  if (F == nullptr || F->isDeclaration())
	    continue;
	  if (calleeWouldBenefitFromInlining(*F) && false)
	    return &C;
	}
	
	return nullptr;
      }

      bool doInitialization(llvm::Module& M) override {
	inline_counts.clear();
	return false;
      }

      bool runOnFunction(llvm::Function& F) override {
	llvm::CallBase *CB;
	std::set<llvm::CallBase *> skip;
	bool changed = false;
	unsigned& inline_count = inline_counts[&F];
	while ((CB = getCallToInline(F, skip))) {
	  if (inline_count >= inline_limit)
	    break;
	  assert(!skip.contains(CB));
	  llvm::Function *CalledF = util::getCalledFunction(CB);
	  if (CalledF == nullptr || CalledF->isDeclaration() || CalledF == &F) {
	    skip.insert(CB);
	    continue;
	  }
	  llvm::InlineFunctionInfo IFI;
	  const auto result = llvm::InlineFunction(*CB, IFI);
	  if (!result.isSuccess()) {
	    skip.insert(CB);
	    continue;
	  }
	  inline_count += inline_counts[CalledF] + 1;
	  changed = true;
	}

	// We'll also erase any mitigations that have been introduced.
	std::vector<MitigationInst *> mitigations;
	for (MitigationInst& I : util::instructions<MitigationInst>(F))
	  mitigations.push_back(&I);
	changed |= !mitigations.empty();
	for (MitigationInst *I : mitigations)
	  I->eraseFromParent();

	assert(llvm::none_of(llvm::instructions(F), [] (const llvm::Instruction& I) {
	  if (const auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I))
	    return II->getIntrinsicID() == llvm::Intrinsic::x86_sse2_lfence;
	  else
	    return false;
	}));
	
	return changed;
      }
    };

#else
    
    struct InlinePass final : public llvm::CallGraphSCCPass {
      static inline char ID = 0;
      InlinePass(): llvm::CallGraphSCCPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<ConstantAddressAnalysis>();
	AU.addRequired<SpeculativeTaint>();
	AU.addRequired<NonspeculativeTaint>();
	AU.addRequired<LeakAnalysis>();
      }

      std::set<llvm::Function *> callees_to_inline; // Functions that would benefit from inlining.
      std::set<llvm::Function *> inlinable; // Functions that we've processed so far.

      std::set<llvm::StoreInst *> compute_nca_stores(llvm::Function& F) {
	auto& CAA = getAnalysis<ConstantAddressAnalysis>(F);
	auto& NST = getAnalysis<NonspeculativeTaint>(F);
	auto& ST = getAnalysis<SpeculativeTaint>(F);
	std::set<llvm::StoreInst *> stores;
	for (llvm::StoreInst& SI : util::instructions<llvm::StoreInst>(F)) {
	  llvm::Value *PtrOp = SI.getPointerOperand();
	  llvm::Value *ValOp = SI.getValueOperand();
	  if (!CAA.isConstantAddress(PtrOp) && (NST.secret(ValOp) || ST.secret(ValOp)))
	    stores.insert(&SI);
	}
	return stores;
      }
      
      bool runOnSCC(llvm::CallGraphSCC& SCC) override {
	if (llvm::all_of(SCC, [] (llvm::CallGraphNode *CGN) {
	  return CGN->getFunction() == nullptr;
	})) {
	  return false;
	}
	
	// 1. Inline callees in this function.

	// Inline functions due to NCAS-CALL in caller.
	for (llvm::CallGraphNode *CGN : SCC) {
	  llvm::Function *F = CGN->getFunction();
	  if (F == nullptr)
	    continue;
	  
	  bool changed;
	  do {
	    changed = false;

	    std::set<llvm::Instruction *> seen;
	    std::set<llvm::CallBase *> calls;
	    std::stack<llvm::Instruction *> todo;
	    for (llvm::StoreInst *SI : compute_nca_stores(*F))
	      todo.push(SI);
	    
	    while (!todo.empty()) {
	      llvm::Instruction *I = todo.top();
	      todo.pop();

	      if (!seen.insert(I).second)
		continue;

	      // If we see a direct call, then note it and stop exploring path.
	      if (auto *C = llvm::dyn_cast<llvm::CallBase>(I)) {
		if (util::mayLowerToFunctionCall(*C)) {
		  calls.insert(C);
		  continue;
		}
	      }

	      // If we see a NCA load that may leak, then stop exploring path.
	      auto& ST = getAnalysis<SpeculativeTaint>(*F);
	      auto& LA = getAnalysis<LeakAnalysis>(*F);						       
	      if (ST.secret(I) && LA.mayLeak(I))
		continue;

	      // TODO: Also check for other stuff?

	      // Otherwise, continue exploring successors.
	      for (llvm::Instruction *Succ : llvm::successors_inst(I))
		todo.push(Succ);
	    }

	    // Try to inline the marked calls.
	    for (llvm::CallBase *Call : calls) {
	      if (!inlinable.contains(util::getCalledFunction(Call)))
		continue;
	      llvm::InlineFunctionInfo IFI;
	      const auto result = llvm::InlineFunction(*Call, IFI);
	      if (!result.isSuccess())
		continue;
	      changed = true;
	    }

	    // Now inline callees that have requested it.
	    for (llvm::CallBase& C : util::instructions<llvm::CallBase>(*F)) {
	      if (callees_to_inline.contains(util::getCalledFunction(&C))) {
		llvm::InlineFunctionInfo IFI;
		const auto result = llvm::InlineFunction(C, IFI);
		if (!result.isSuccess())
		  continue;
		changed = true;
	      }
	    }

	  } while (changed && false);
	}

	// 2. Calculate whether this function would benefit from inlining into caller.
	if (SCC.size() == 1) {
	  llvm::Function& F = *(**SCC.begin()).getFunction();
	  bool ncas_ret = false;
	  std::stack<llvm::Instruction *> todo;
	  std::set<llvm::Instruction *> seen;
	  for (llvm::StoreInst *SI : compute_nca_stores(F))
	    todo.push(SI);
	  while (!todo.empty()) {
	    auto *I = todo.top();
	    todo.pop();
	    if (!seen.insert(I).second)
	      continue;

	    if (llvm::isa<llvm::ReturnInst>(I)) {
	      ncas_ret = true;
	      break;
	    }
	    
	    if (auto *C = llvm::dyn_cast<llvm::CallBase>(I))
	      if (util::mayLowerToFunctionCall(*C))
		continue;

	    auto& ST = getAnalysis<SpeculativeTaint>(F);
	    auto& LA = getAnalysis<LeakAnalysis>(F);
	    if (ST.secret(I) && LA.mayLeak(I))
	      continue;

	    for (auto *Succ : llvm::successors_inst(I))
	      todo.push(Succ);
	  }

	  if (ncas_ret)
	    callees_to_inline.insert(&F);
	}

	// 3. Add inlinable functions.
	{
	  const auto defined_funcs = llvm::count_if(SCC, [] (auto *CGN) {
	    return CGN->getFunction() != nullptr;
	  });
	  if (defined_funcs == 1)
	    for (auto *CGN : SCC)
	      if (auto *F = CGN->getFunction())
		inlinable.insert(F);
	}

	return true;
      }
    };

#endif
    

    llvm::RegisterPass<InlinePass> X {"clou-inline-hints", "LLVM-SCT's Inlining Pass"};
    util::RegisterClangPass<InlinePass> Y;
  }
}
