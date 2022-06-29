#include <llvm/IR/Instruction.h>

bool has_incoming_addr(const llvm::Instruction *I);
bool has_incoming_addr(const llvm::Value *V);



