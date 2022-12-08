/* CFG Utils
 * Nicholas Mosier - 12/08/2022
 */

#pragma once

#include <set>

#include <llvm/IR/Instruction.h>

namespace clou {

  std::set<llvm::Instruction *> getInstructionsReachableFrom(llvm::Instruction *start, bool forward = true, bool inclusive = false); 

  /* Get the set of instructions along all possible control-flow paths between two instructions (w/i the same func)
   * Inclusive: doesn't include the start/stop instructions
   */
  std::set<llvm::Instruction *> getInstructionsBetween(llvm::Instruction *start, llvm::Instruction *stop);
  
}
