#pragma once

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/CallGraphSCCPass.h>
#include <llvm/Analysis/CallGraph.h>

#include <ostream>
#include <set>
#include <sstream>
#include <string>

namespace util {
// helper type for the visitor #4
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
} // namespace util

bool has_incoming_addr(const llvm::Value *V);

namespace impl {
template <class OutputIt>
OutputIt get_incoming_loads(llvm::Value *V, OutputIt out,
                            std::set<llvm::Value *> &seen) {
  if (!seen.insert(V).second) {
    return out;
  }

  if (llvm::isa<llvm::Argument, llvm::LoadInst, llvm::CallBase>(V)) {
    *out++ = V;
  } else if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
    for (llvm::Value *op : I->operands()) {
      out = get_incoming_loads(op, out, seen);
    }
  }

  return out;
}
} // namespace impl

template <class OutputIt>
OutputIt get_incoming_loads(llvm::Value *V, OutputIt out) {
  std::set<llvm::Value *> seen;
  return impl::get_incoming_loads(V, out, seen);
}

std::set<llvm::Value *> get_incoming_loads(llvm::Value *V);

template <class Inst, class Func>
void for_each_inst(llvm::Function &F, Func func) {
  for (llvm::BasicBlock &B : F) {
    for (llvm::Instruction &I : B) {
      if (Inst *I_ = llvm::dyn_cast<Inst>(&I)) {
        func(I_);
      }
    }
  }
}

template <class Inst, class Func>
void for_each_inst(const llvm::Function& F, Func func) {
    for (const llvm::BasicBlock& B : F) {
        for (const llvm::Instruction& I : B) {
            if (const Inst *I_ = llvm::dyn_cast<Inst>(&I)) {
                func(I_);
            }
        }
    }
}

template <class Inst, class Func>
void for_each_inst(llvm::CallGraphSCC& SCC, Func func) {
    for (llvm::CallGraphNode *CGN : SCC) {
        if (llvm::Function *F = CGN->getFunction()) {
            if (!F->isDeclaration()) {
                for_each_inst<Inst>(*F, func);
            }
        }
    }
}

template <class Inst, class Func, class InputIt>
void for_each_inst(InputIt begin, InputIt end, Func func) {
    for_each_func_def(begin, end, [&func] (llvm::Function& F) {
        for_each_inst<Inst>(F, func);
    });
}

template <class Func>
void for_each_func_def(llvm::Module& M, Func func) {
    for (llvm::Function& F : M) {
        if (!F.isDeclaration()) {
            func(F);
        }
    }
}

template <class Func>
void for_each_func_def(const llvm::Module& M, Func func) {
    for (const llvm::Function& F : M) {
        if (!F.isDeclaration()) {
            func(F);
        }
    }
}

template <class Func>
void for_each_func_def(llvm::CallGraphSCC& SCC, Func func) {
    for (llvm::CallGraphNode *CGN : SCC) {
        if (llvm::Function *F = CGN->getFunction()) {
            if (!F->isDeclaration()) {
                func(*F);
            }
        }
    }
}

template <class Func, class InputIt>
void for_each_func_def(InputIt begin, InputIt end, Func func) {
    for (InputIt it = begin; it != end; ++it) {
        if (llvm::Function *F = *it) {
            if (!F->isDeclaration()) {
                func(*F);
            }
        }
    }
}

bool is_speculative_secret(const llvm::Instruction *I);
bool is_speculative_secret(const llvm::Value *V);

bool is_nonspeculative_secret(const llvm::Value *V);
bool is_nonspeculative_secret(const llvm::Instruction *I);

std::ostream &operator<<(std::ostream &os, const llvm::Value &V);

std::string to_string(const llvm::Value *V);

namespace util {

template <class T> std::string to_string(const T &x) {
  std::stringstream ss;
  ss << x;
  return ss.str();
}

} // namespace util

// compute loop depth
unsigned instruction_loop_nest_depth(llvm::Instruction *I, const llvm::LoopInfo& LI);
unsigned instruction_dominator_depth(llvm::Instruction *I, const llvm::DominatorTree& DT);

namespace llvm {

std::vector<llvm::Instruction *> predecessors(llvm::Instruction *I);

/**
 * Guaranteed to produce output in reverse dominating order.
 */
template <class Inst, class OutputIt>
OutputIt dominators(llvm::DominatorTree& DT, llvm::Instruction *I, OutputIt out) {
    const auto output = [&out] (llvm::Instruction *I) {
        if (Inst *I_ = llvm::dyn_cast<Inst>(I)) {
            *out++ = I_;
        }
    };
    
    // dominators w/i basic block
    for (llvm::Instruction *I_ = I; I_; I_ = I_->getPrevNode()) {
        output(I_);
    }
    
    // dominating basic blocks
    for (auto *node = DT[I->getParent()]->getIDom(); node; node = node->getIDom()) {
        for (llvm::Instruction& I : llvm::reverse(*node->getBlock())) {
            output(&I);
        }
    }

    return out;
}

}

#define unhandled_instruction(I)		\
  do {									\
    llvm::errs() << __FILE__ << ":" << __LINE__ << ":" << I << "\n";	\
    std::abort();							\
  } while (false)
