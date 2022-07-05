#include <llvm/IR/Instruction.h>

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

std::set<llvm::Value *> get_incoming_loads(llvm::Instruction *I);
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

template <class OutputIt>
OutputIt get_transmitter_sensitive_operands(llvm::Instruction *I,
                                            OutputIt out) {
  if (llvm::isa<llvm::LoadInst, llvm::StoreInst>(I)) {
    *out++ = llvm::getPointerOperand(I);
  } else if (llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(I)) {
    if (BI->isConditional()) {
      *out++ = BI->getCondition();
    }
  } else if (llvm::CallBase *C = llvm::dyn_cast<llvm::CallBase>(I)) {
    for (llvm::Value *op : C->operands()) {
      *out++ = op;
    }
  } else if (llvm::ReturnInst *RI = llvm::dyn_cast<llvm::ReturnInst>(I)) {
    if (llvm::Value *RV = RI->getReturnValue()) {
      *out++ = RV;
    }
  }
  return out;
}

std::set<llvm::Value *>
get_transmitter_sensitive_operands(llvm::Instruction *I);

namespace llvm {

std::vector<llvm::Instruction *> predecessors(llvm::Instruction *I);

}
