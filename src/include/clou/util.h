#pragma once

#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <initializer_list>
#include <vector>
#include <cstdlib>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/CallGraphSCCPass.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/ADT/iterator_range.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/iterator.h>

namespace clou {

  namespace util {
    // helper type for the visitor #4
    template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    // explicit deduction guide (not needed as of C++20)
    template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
  } // namespace util

  bool has_incoming_addr(const llvm::Value *V);
  bool is_nonconstant_value(const llvm::Value *V);

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

  namespace impl {
    inline const llvm::Value& get_value_ref(const llvm::Value& V) { return V; }
    inline const llvm::Value& get_value_ref(const llvm::Value *V) { return *V; }
  }

#define unhandled_value(V)						\
  do {									\
    llvm::errs() << __FILE__ << ":" << __LINE__ << ": unhandled value: " << ::clou::impl::get_value_ref(V) << "\n"; \
    std::_Exit(EXIT_FAILURE);						\
  } while (false)

#define unhandled_instruction(I) unhandled_value(I)

  namespace util {

    /**
     * Provide a more complete version of llvm::CallBase::getCalledFunction() that handles more cases.
     */
    llvm::Function *getCalledFunction(const llvm::CallBase *C);

    bool functionIsDirectCallOnly(const llvm::Function& F);

    template <class Pass>
    class RegisterClangPass {
    public:
      RegisterClangPass(): RegisterClangPass({llvm::PassManagerBuilder::EP_OptimizerLast, llvm::PassManagerBuilder::EP_EnabledOnOptLevel0}) {}
      RegisterClangPass(std::initializer_list<llvm::PassManagerBuilder::ExtensionPointTy> extension_points) {
	for (auto extension_point : extension_points) {
	  extension_ids.push_back(llvm::PassManagerBuilder::addGlobalExtension(extension_point, &registerPass));
	}
      }

      ~RegisterClangPass() {
	for (auto extension_id : extension_ids) {
	  llvm::PassManagerBuilder::removeGlobalExtension(extension_id);
	}
      }
    
    private:
      std::vector<llvm::PassManagerBuilder::GlobalExtensionID> extension_ids;

      static void registerPass(const llvm::PassManagerBuilder&, llvm::legacy::PassManagerBase& PM) {
	PM.add(new Pass());
      }
    };

    bool isSpeculativeInbounds(llvm::StoreInst *SI);

    // TODO: template, and explicitly specialize to llvm::Function?
    // Directly iterate over insturctions in functions.

    template <class OuterIt, class InnerIt>
    class NestedIterator {
    public:

      NestedIterator() = default;
      template <class Outer>
      NestedIterator(const Outer& o): NestedIterator(o.begin(), o.end()) {}
      NestedIterator(OuterIt o_begin, OuterIt o_end): o_it(o_begin), o_end(o_end) {
	if (o_it != o_end) {
	  advance();
	}
      }
    
      auto& operator++() {
	next();
	return *this;
      }
    
      auto& operator++(int) {
	next();
	return *this;
      }
    
      auto& operator*() const {
	assert(!done());
	return *i_it;
      }
    
      auto *operator->() const {
	assert(!done());
	return &*i_it;
      }

    private:
      OuterIt o_it;
      OuterIt o_end;
      InnerIt i_it;
      InnerIt i_end;

      bool done() const {
	return !(o_it == o_end && i_it == i_end);
      }

      void next() {
	assert(i_it != i_end);
	++i_it;
	advance();
      }
    
      void advance() {
	assert(o_it != o_end);
	while (i_it == i_end) {
	  ++o_it;
	  if (o_it == o_end) {
	    return;
	  }
	  i_it = o_it->begin();
	}
      }
    };

    using InstructionFunctionIterator = NestedIterator<llvm::Function::iterator, llvm::BasicBlock::iterator>;
    inline InstructionFunctionIterator instructions_begin(llvm::Function& F) {
      return InstructionFunctionIterator(F.begin(), F.end());
    }

    inline InstructionFunctionIterator instructions_end(llvm::Function& F) {
      return InstructionFunctionIterator(F.end(), F.end());
    }

    using InstructionFunctionRange = llvm::iterator_range<InstructionFunctionIterator>;
    inline InstructionFunctionRange instructions(llvm::Function& F) {
      return InstructionFunctionRange(instructions_begin(F), instructions_end(F));
    }

    // TODO: Double-check this using test pass.
  }

}

namespace llvm {

  std::vector<llvm::Instruction *> predecessors(llvm::Instruction *I);
  std::vector<llvm::Instruction *> successors_inst(llvm::Instruction *I);
  using loop_pred_range = std::vector<llvm::BasicBlock *>;
  loop_pred_range predecessors(llvm::Loop *L);
  using loop_succ_range = llvm::SmallVector<llvm::BasicBlock *, 4>;
  loop_succ_range successors(llvm::Loop *L);

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

namespace clou::impl {
  void warn_unhandled_intrinsic_(llvm::Intrinsic::ID id, const char *file, size_t line);
  void warn_unhandled_intrinsic_(const llvm::IntrinsicInst *II, const char *file, size_t line);
#define warn_unhandled_intrinsic(id) ::clou::impl::warn_unhandled_intrinsic_(id, __FILE__, __LINE__)
}

namespace clou {
  
  size_t countNonDebugInstructions(const llvm::BasicBlock& B);

  void trace(const char *fmt, ...);

  namespace util {

    namespace impl {
      
      template <class InstType>
      struct inst_filter_functor {
	bool operator()(const llvm::Instruction& I) const {
	  return llvm::isa<InstType>(&I);
	}
      };

      template <class InstType>
      struct inst_functor {
	InstType& operator()(llvm::Instruction& I) const {
	  return *llvm::cast<InstType>(&I);
	}
      };

      template <class InstType>
      using inst_filter_iterator = llvm::filter_iterator<llvm::inst_iterator, inst_filter_functor<InstType>>;
    }
    
    template <class InstType>
    using inst_iterator = llvm::mapped_iterator<impl::inst_filter_iterator<InstType>, impl::inst_functor<InstType>>;

    namespace impl {
      template <class InstType>
      inst_iterator<InstType> make_inst_iterator(llvm::inst_iterator it, llvm::inst_iterator end) {
	static_assert(std::is_base_of_v<llvm::Instruction, InstType>,
		      "template parameter InstType must be subclass of llvm::Instruction");
	return inst_iterator<InstType>(impl::inst_filter_iterator<InstType>(it, end, impl::inst_filter_functor<InstType>()), impl::inst_functor<InstType>());
      }
    }
    
    template <class InstType>
    inst_iterator<InstType> inst_begin(llvm::Function& F) {
      return impl::make_inst_iterator<InstType>(llvm::inst_begin(F), llvm::inst_end(F));
    }

    template <class InstType>
    inst_iterator<InstType> inst_end(llvm::Function& F) {
      return impl::make_inst_iterator<InstType>(llvm::inst_end(F), llvm::inst_end(F));
    }

    template <class InstType>
    llvm::iterator_range<inst_iterator<InstType>> instructions(llvm::Function& F) {
      return llvm::iterator_range(inst_begin<InstType>(F), inst_end<InstType>(F));
    }

    namespace impl {
      struct nonvoid_inst_predicate {
	bool operator()(const llvm::Instruction& I) const;
      };
    }

    using nonvoid_inst_iterator = llvm::filter_iterator<llvm::inst_iterator, impl::nonvoid_inst_predicate>;

    namespace impl {
      nonvoid_inst_iterator make_nonvoid_inst_iterator(llvm::inst_iterator it, llvm::inst_iterator end);
    }

    nonvoid_inst_iterator nonvoid_inst_begin(llvm::Function& F);
    nonvoid_inst_iterator nonvoid_inst_end(llvm::Function& F);
    llvm::iterator_range<nonvoid_inst_iterator> nonvoid_instructions(llvm::Function& F);

    llvm::Value *getPointerOperand(llvm::Instruction *I);
    llvm::SmallVector<llvm::Value *, 3> getAccessOperands(llvm::Instruction *I);
    llvm::SmallVector<llvm::Value *, 3> getValueOperands(llvm::Instruction *I);
    llvm::Value *getConditionOperand(llvm::Instruction *I);

    namespace impl {
      template <class Stream, class... Ts>
      void make_string(Stream& os, const Ts&... args) {
	((os << args), ...);
      }
    }

    template <class... Ts>
    std::string make_string_std(const Ts&... args) {
      std::stringstream ss;
      impl::make_string(ss, args...);
      return ss.str();
    }

    template <class... Ts>
    std::string make_string_llvm(const Ts&... args) {
      std::string s;
      llvm::raw_string_ostream os(s);
      impl::make_string(os, args...);
      return s;
    }

    bool isConstantAddress(const llvm::Value *V);
  }
}

