#include "clou/analysis/NonspeculativeTaintAnalysis.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/analysis/LeakAnalysis.h"
#include "clou/Mitigation.h"
#include "clou/FordFulkerson.h"
#include "clou/util.h"
#include "clou/Transmitter.h"

#include <llvm/Pass.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/Clou/Clou.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include <stack>

namespace clou {
  namespace {

    struct NCAStorePass final : public llvm::FunctionPass {
      static inline char ID = 0;
      NCAStorePass(): llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<NonspeculativeTaint>();
	AU.addRequired<SpeculativeTaint>();
	AU.addRequired<LeakAnalysis>();
      }

      static bool shouldCutEdge(llvm::Instruction *src, llvm::Instruction *dst) {
	return llvm::predecessors(dst).size() > 1 || llvm::successors_inst(src).size() > 1;
      }

      static llvm::Instruction *getMitigationPoint(llvm::Instruction *src, llvm::Instruction *dst) {
	if (shouldCutEdge(src, dst)) {
	  assert(src->isTerminator());
	  for (const llvm::Instruction *I = dst->getPrevNode(); I != nullptr; I = I->getPrevNode())
	    assert(llvm::isa<MitigationInst>(I));
	  if (dst != &dst->getParent()->front())
	    return nullptr;
	  llvm::BasicBlock *B = llvm::SplitEdge(src->getParent(), dst->getParent());
	  return &B->front();
	} else {
	  return dst;
	}
      }

      static bool ignoreCall(const llvm::CallBase *C) {
	if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(C)) {
	  if (llvm::isa<MitigationInst, llvm::DbgInfoIntrinsic>(II) ||  II->isAssumeLikeIntrinsic()) {
	    return true;
	  } else {
	    switch (II->getIntrinsicID()) {
	    case llvm::Intrinsic::fshr:
	    case llvm::Intrinsic::fshl:
	    case llvm::Intrinsic::x86_aesni_aesenc:
	    case llvm::Intrinsic::x86_aesni_aeskeygenassist:
	    case llvm::Intrinsic::x86_aesni_aesenclast:
	    case llvm::Intrinsic::vector_reduce_and:
	    case llvm::Intrinsic::vector_reduce_or:
	    case llvm::Intrinsic::umax:
	    case llvm::Intrinsic::umin:
	    case llvm::Intrinsic::smax:
	    case llvm::Intrinsic::smin:
	    case llvm::Intrinsic::ctpop:
	    case llvm::Intrinsic::bswap:
	    case llvm::Intrinsic::x86_pclmulqdq:
	    case llvm::Intrinsic::x86_rdrand_32:
	    case llvm::Intrinsic::vastart:
	    case llvm::Intrinsic::vaend:
	    case llvm::Intrinsic::vector_reduce_add:
	    case llvm::Intrinsic::abs:
	    case llvm::Intrinsic::umul_with_overflow:
	    case llvm::Intrinsic::bitreverse:
	    case llvm::Intrinsic::cttz:
	    case llvm::Intrinsic::usub_sat:
	    case llvm::Intrinsic::fmuladd:
	    case llvm::Intrinsic::annotation:
	    case llvm::Intrinsic::x86_sse2_mfence:
	    case llvm::Intrinsic::fabs:
	    case llvm::Intrinsic::floor:
	      return true;

	    case llvm::Intrinsic::memset:
	    case llvm::Intrinsic::memcpy:
	    case llvm::Intrinsic::memmove:
	      return false;

	    default:
	      warn_unhandled_intrinsic(II);
	      return false;
	    }
	  }
	} else {
	  return false;
	}	
      }

      static unsigned compute_edge_weight(llvm::Instruction *src, llvm::Instruction *dst,
					  const llvm::DominatorTree& DT, const llvm::LoopInfo& LI) {
	float score = 1.;
	const unsigned LoopDepth = std::min(instruction_loop_nest_depth(src, LI), instruction_loop_nest_depth(dst, LI));
	const unsigned DomDepth = std::max(instruction_dominator_depth(src, DT), instruction_dominator_depth(dst, DT));
	score *= LoopWeight * static_cast<float>(LoopDepth + 1);
	score *= 1. / static_cast<float>(DominatorWeight * (DomDepth + 1));
	return score * 1000;
      }
      
      bool runOnFunction(llvm::Function& F) override {
	llvm::errs() << getPassName() << " @ " << F.getName() << "\n";
	auto& NST = getAnalysis<NonspeculativeTaint>();
	auto& ST = getAnalysis<SpeculativeTaint>();
	[[maybe_unused]] auto& LA = getAnalysis<LeakAnalysis>();

	llvm::DominatorTree DT(F);
	llvm::LoopInfo LI(DT);

	// Set of nt-/t-secret NCA stores
	std::set<llvm::Instruction *> ncas_sec;
	for (llvm::Instruction& I : llvm::instructions(F)) {
	  if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
	    if (!util::isConstantAddressStore(SI))
	      continue;
	    auto *V = SI->getValueOperand();
	    if (NST.secret(V) || ST.secret(V))
	      ncas_sec.insert(SI);
	  }
	}

	// Set of inter-prodecural control-transfer instructions
	std::set<llvm::Instruction *> ctrls;
	for (auto& I : llvm::instructions(F)) {
	  if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I))
	    if (!ignoreCall(CB))
	      ctrls.insert(CB);
	  if (auto *RI = llvm::dyn_cast<llvm::ReturnInst>(&I))
	    ctrls.insert(RI);
	}

	// Set of non-constant value transmitters
	std::set<llvm::Instruction *> xmits;
	for (auto& I : llvm::instructions(F)) {
	  for (const TransmitterOperand& TO : get_transmitter_sensitive_operands(&I)) {
	    if (!get_incoming_loads(TO.V).empty()) {
	      xmits.insert(&I);
	      continue;
	    }
	  }
	}


	// Construct graph
	{
	  std::vector<llvm::Instruction *> nodes;
	  for (auto& I : llvm::instructions(F))
	    nodes.push_back(&I);
	  llvm::sort(nodes);
	  const auto node_to_idx = [&nodes] (llvm::Instruction *I) -> int {
	    const auto it = llvm::lower_bound(nodes, I);
	    assert(it != nodes.end() && *it == I);
	    return static_cast<int>(it - nodes.begin());
	  };
	  const auto idx_to_node = [&nodes] (int idx) -> llvm::Instruction * {
	    assert(idx >= 0 && static_cast<size_t>(idx) < nodes.size());
	    return nodes[idx];
	  };
	  int n = nodes.size();
	  const int super_s = n++;
	  const int super_t = n++;
	  
	  std::vector<std::map<int, int>> G(n);
	  for (auto& src_I : llvm::instructions(F)) {
	    if (llvm::isa<MitigationInst>(&src_I))
	      continue;
	    const int src_idx = node_to_idx(&src_I);
	    for (auto *dst_I : llvm::successors_inst(&src_I)) {
	      if (llvm::isa<MitigationInst>(dst_I))
		continue;
	      const int dst_idx = node_to_idx(dst_I);
	      G[src_idx][dst_idx] = compute_edge_weight(&src_I, dst_I, DT, LI);
	    }
	  }

	  // Add supernodes
	  for (auto *I : ncas_sec)
	    G[super_s][node_to_idx(I)] = std::numeric_limits<int>::max();
	  for (auto *I : llvm::concat<llvm::Instruction * const>(ctrls, xmits))
	    G[node_to_idx(I)][super_t] = std::numeric_limits<int>::max();

	  // Run algorithm
	  const auto cut_edges_idx = ford_fulkerson(n, G, super_s, super_t);
	  std::set<std::pair<llvm::Instruction *, llvm::Instruction *>> cut_edges;
	  for (const auto& [src_idx, dst_idx] : cut_edges_idx) {
	    assert(static_cast<size_t>(src_idx) < nodes.size());
	    assert(static_cast<size_t>(dst_idx) < nodes.size());
	    cut_edges.emplace(idx_to_node(src_idx), idx_to_node(dst_idx));
	  }

	  // Double-check cut
	  {
	    for (auto *s : ncas_sec) {
	      // Find reachable nodes.
	      std::set<llvm::Instruction *> seen;
	      std::stack<llvm::Instruction *> todo;
	      todo.push(s);
	      while (!todo.empty()) {
		auto *src = todo.top();
		todo.pop();
		if (!seen.insert(src).second)
		  continue;
		for (auto *dst : llvm::successors_inst(src))
		  todo.push(dst);
	      }

	      for (auto *t : llvm::concat<llvm::Instruction * const>(ctrls, xmits)) {
		if (seen.contains(t)) {
		  llvm::errs() << getPassName() << ": error: found s-t path\n";
		  llvm::errs() << "s:   " << *s << "\n";
		  llvm::errs() << "t:   " << *t << "\n";
		  std::abort();
		}
	      }
	    }
	  }

	  // Insert mitigations.
	  for (const auto& [src, dst] : cut_edges) {
	    if (llvm::Instruction *mitigation_point = getMitigationPoint(src, dst)) {
	      CreateMitigation(mitigation_point, "clou-mitigate-pass");
	    }
	  }

	  return true;
	}
	
      }

    };

    llvm::RegisterPass<NCAStorePass> X("clou-ncas-store", "Clou NCA Store Pass");
    util::RegisterClangPass<NCAStorePass> Y;
  }
}
