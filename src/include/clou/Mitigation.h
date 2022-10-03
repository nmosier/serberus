#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instruction.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Constants.h>

namespace clou {

  class MitigationInst : public llvm::IntrinsicInst {
  public:
    static inline const char mitigation_flag[] = "clou.mitigation";
    static bool classof(const llvm::IntrinsicInst *I);
    static bool classof(const llvm::Value *V);
    void setIdentifier(uint64_t id);
    llvm::ConstantInt *getIdentifier() const;
    llvm::StringRef getDescription() const;
  };

  MitigationInst *CreateMitigation(llvm::Instruction *I, const char *lfencestr);
  MitigationInst *CreateMitigation(llvm::IRBuilder<>& IRB, const char *lfencestr);  
  
}
