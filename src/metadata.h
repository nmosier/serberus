#pragma once

#include <llvm/IR/Instruction.h>
#include <llvm/ADT/StringRef.h>

namespace clou::md {
  
  inline const char speculative_inbounds[] = "specinbounds";
  inline const char nospill[] = "clou.nospill";

  void setMetadataFlag(llvm::Instruction *I, llvm::StringRef flag);
  bool getMetadataFlag(const llvm::Instruction *I, llvm::StringRef flag);  
  
}
