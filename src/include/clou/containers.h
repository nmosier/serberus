#pragma once

#include <set>
#include <queue>
#include <map>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>

namespace clou {

  using VSet = std::set<llvm::Value *>;
  using VSetSet = std::set<VSet>;
  using VMap = std::map<llvm::Value *, VSet>;
  using ISet = std::set<llvm::Instruction *>;
  using IQueue = std::queue<llvm::Instruction *>;
  
}
