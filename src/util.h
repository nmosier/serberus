#pragma once

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>

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

}
