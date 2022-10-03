#include <memory>
#include <map>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/IntrinsicInst.h>

#include "clou/util.h"

using namespace llvm;

namespace clou {

constexpr const char *sep = "_"; // TODO: get rid of

namespace {

  constexpr uint64_t stack_size = 0x20000; // TODO: make these command-line parameters
  const Align max_align (256); // TODO: make command-line parameter

  struct FunctionLocalStacks final: public ModulePass {
    static char ID;

    FunctionLocalStacks(): ModulePass(ID) {}

    virtual bool runOnModule(Module& M) override {
      // for every function declaration, emit tentative wrappers
      {
	std::vector<Function *> todo;
	std::transform(M.begin(), M.end(), std::back_inserter(todo), [] (Function& F) { return &F; });
	for (Function *F : todo) {
	  if (F->isDeclaration()) {
	    emitWrapperForDeclaration(*F);
	  } else {
	    emitAliasForDefinition(*F);
	  }
	}
      }

      replaceMalloc(M);
      replaceMemoryRealloc(M, "realloc");
      replaceMemoryRealloc(M, "reallocarray");
      
      std::vector<GlobalVariable *> GVs;
      
      for (Function& F : M) {
	if (!F.isDeclaration()) {
	  runOnFunction(F, std::back_inserter(GVs));
	}
      }

      return true;
    }

    template <class OutputIt>
    void runOnFunction(Function& F, OutputIt out) {
      F.addFnAttr(Attribute::get(F.getContext(), "stackrealign"));
      
      Module& M = *F.getParent();

      // Type:
      Type *stack_ty = ArrayType::get(IntegerType::getInt8Ty(F.getContext()), stack_size);
      Type *sp_ty = PointerType::get(IntegerType::getInt8Ty(F.getContext()), 0);
      const auto stack_name = (F.getName() + sep + "stack").str();
      const auto sp_name = (F.getName() + sep + "sp").str();

      // determine correct linkage
      GlobalVariable::LinkageTypes linkage = GlobalVariable::LinkageTypes::InternalLinkage;

      GlobalVariable *stack = new GlobalVariable(M, stack_ty, false, linkage, Constant::getNullValue(stack_ty), stack_name, nullptr, GlobalValue::NotThreadLocal);
      GlobalVariable *sp = new GlobalVariable(M,
					      sp_ty,
					      false,
					      linkage,
					      ConstantExpr::getBitCast(ConstantExpr::getGetElementPtr(stack_ty, stack,
												      Constant::getIntegerValue(Type::getInt8Ty(M.getContext()),
																APInt(8, 1))),
								       sp_ty),
					      sp_name, nullptr, GlobalValue::NotThreadLocal);
      stack->setDSOLocal(true);
      sp->setDSOLocal(true);
      stack->setAlignment(max_align);
      sp->setAlignment(Align(8)); // TODO: actually compute size of pointer?

      *out++ = stack;
      *out++ = sp;

      // Save old function-local stack pointer on entry + exit
      LoadInst *LI = IRBuilder<>(&F.getEntryBlock().front()).CreateLoad(sp_ty, sp);
      for (BasicBlock &B : F) {
	Instruction *I = &B.back();
	if (isa<ReturnInst>(I)) {
	  IRBuilder<>(I).CreateStore(LI, sp);
	}
      }
    }

    CallInst *findCallToFunction(Function& F, StringRef name) {
      for (BasicBlock& B : F) {
	for (Instruction& I : B) {
	  if (CallInst *C = dyn_cast<CallInst>(&I)) {
	    if (Function *callee = C->getCalledFunction()) {
	      if (callee->getName() == name) {
		return C;
	      }
	    }
	  }
	}
      }
      return nullptr;
    }

    void replaceMalloc(Module& M) {
      auto& ctx = M.getContext();
      if (Function *F = M.getFunction("__clou_wrap_malloc")) {
	// find call instruction
	for (BasicBlock& B : *F) {
	  for (Instruction& I : B) {
	    if (CallInst *C = dyn_cast<CallInst>(&I)) {
	      if (Function *callee = C->getCalledFunction()) {
		if (callee->getName() == "malloc") {
		  Type *I64 = Type::getInt64Ty(ctx);
		
		  // get declaration of calloc
		  Function *callocF = M.getFunction("calloc");
		  if (callocF == nullptr) {
		    std::vector<Type *> args = {I64, I64};
		    FunctionType *callocT = FunctionType::get(Type::getInt8PtrTy(ctx), args, false);
		    callocF = Function::Create(callocT, Function::LinkageTypes::ExternalLinkage, "calloc", M);
		  }


		  IRBuilder<> IRB (C);
		  std::vector<Value *> args = {F->getArg(0), ConstantInt::get(I64, 1)};
		  CallInst *newC = IRB.CreateCall(callocF, args);
#if 0
		  for (User *U : C->users()) {
		    U->replaceUsesOfWith(C, newC);
		  }
#else
		  C->replaceAllUsesWith(newC);
#endif
		  C->eraseFromParent();

		  return;
		}
	      }
	    }
	  }
	}
      }

    }

    Function *getMallocUsableSizeDecl(Module& M) {
      StringRef name = "malloc_usable_size";
      if (Function *F = M.getFunction(name)) {
	return F;
      } else {
	LLVMContext& ctx = M.getContext();
	FunctionType *T = FunctionType::get(Type::getInt64Ty(ctx), std::vector<Type *> {Type::getInt8PtrTy(ctx)}, false);
	return Function::Create(T, Function::ExternalLinkage, name, M);
      }
    }

    void replaceMemoryRealloc(Function& wrapF, StringRef realName) {
      LLVMContext& ctx = wrapF.getContext();
      Module& M = *wrapF.getParent();
      CallInst *reallocPtr = findCallToFunction(wrapF, realName);
      if (reallocPtr == nullptr) { errs() << wrapF << "\n"; }
      assert(reallocPtr != nullptr);
      IRBuilder<> preIRB (reallocPtr);
      IRBuilder<> postIRB (reallocPtr->getNextNode());
      Function *mallocUsableSizeF = getMallocUsableSizeDecl(M);

      CallInst *oldSize =  preIRB.CreateCall(mallocUsableSizeF, std::vector<Value *> {wrapF.getArg(0)});
      CallInst *newSize = postIRB.CreateCall(mallocUsableSizeF, std::vector<Value *> {reallocPtr});
      Value *sizeCmp    = postIRB.CreateICmpULT(oldSize, newSize);
      Value *sizeDiff   = postIRB.CreateSub(newSize, oldSize);
      Value *sizeDiffOrZero = postIRB.CreateSelect(sizeCmp, sizeDiff, Constant::getNullValue(Type::getInt64Ty(ctx)));
      Value *gep = postIRB.CreateInBoundsGEP(reallocPtr->getType()->getPointerElementType(), reallocPtr,
					     std::vector<Value *> {oldSize});
      postIRB.CreateMemSet(gep, Constant::getNullValue(Type::getInt8Ty(ctx)), sizeDiffOrZero, MaybeAlign(16));
    }

    void replaceMemoryRealloc(Module& M, StringRef realName) {
      const Twine wrapName = wrapperName(realName);
      if (Function *F = M.getFunction(wrapName.str())) {
	replaceMemoryRealloc(*F, realName);
      }
    }
      
    InlineAsm *makeFence(LLVMContext& ctx) {
      return InlineAsm::get(FunctionType::get(Type::getVoidTy(ctx), false), "lfence", "",
			    false, InlineAsm::AD_Intel);
    }

    Twine wrapperName(StringRef name) {
      return "__clou_wrap_" + name;
    }

    Twine wrapperName(Function& F) {
      return wrapperName(F.getName());
    }

    std::string wrapperName(Function& F, unsigned& counter) {
      return "__clou_wrap" + std::to_string(counter++) + "_" + F.getName().str();
    }

    std::string nonVariadicName(Function& F) {
      std::string name = F.getName().str();
      std::string::iterator it;
      for (it = name.begin(); it != name.end() && *it == '_'; ++it) {}
      name.insert(it, 'v');
      errs() << "non-variadic: " << name << "\n";
      return name; 
    }

    static bool isUntyped(const Function& F) {
      return F.isVarArg() && F.arg_size() == 0;
    }

    void emitWrapperForDeclaration(Function& F) {
      FunctionType *T = nullptr;
      
      if (F.isIntrinsic()) {
	return;
      }

      if (isUntyped(F)) {

	errs() << "CLOU: warning: calls to untyped function \"" << F.getName() << "\" will not be mitigated\n";
	return; 
	
      } else if (F.isVarArg()) {

	emitWrappersForVarArgDecl(F);
	return;
	
      }

      Module& M = *F.getParent();
      auto& ctx = M.getContext();

      // emit weak function definition __clou_<funcname>
      if (T == nullptr) {
	T = cast<FunctionType>(F.getType()->getPointerElementType());
      }
      Function *newF = Function::Create(T, Function::LinkageTypes::WeakAnyLinkage, wrapperName(F), M);
      BasicBlock *B = BasicBlock::Create(ctx, "", newF);
      IRBuilder<> IRB (B);

      // entering fence
      IRB.CreateCall(makeFence(ctx));

      // call to external function
      std::vector<Value *> args;
      for (Argument& A : newF->args()) {
	args.push_back(&A);
      }
      Instruction *C = IRB.CreateCall(T, &F, args);
      
      // exiting fence
      IRB.CreateCall(makeFence(ctx));

      // return
      if (C->getType()->isVoidTy()) {
	IRB.CreateRetVoid();
      } else {
	IRB.CreateRet(C);
      }

      replaceUses(&F, newF);
    }

    static const std::map<std::string, std::vector<std::function<Type * (LLVMContext&)>>> std_finite_varargs;

    bool lookupFiniteVarArgFunc(Function& F, std::vector<Type *>& Ts) {
      LLVMContext& ctx = F.getContext();

      const auto it = std_finite_varargs.find(F.getName().str());
      if (it == std_finite_varargs.end()) {
	return false;
      }
      
      std::transform(it->second.begin(), it->second.end(), std::back_inserter(Ts),
		     [&ctx] (const auto& f) -> Type * {
		       return f(ctx);
		     });
      return true;
    }
    

    bool handleFiniteVarArgFunc(Function& F) {
      assert(F.isDeclaration());

      std::vector<Type *> Ts;
      if (!lookupFiniteVarArgFunc(F, Ts)) {
	return false;
      }

      Function *newF = emitWrapperForFiniteVarArgFunc(F, Ts);
      
      // replace uses of F with newF, and update calls
      for (User *user : F.users()) {
	if (CallInst *C = dyn_cast<CallInst>(user)) {
	  IRBuilder<> IRB (C);
	  std::vector<Value *> args;
	  std::copy(C->arg_begin(), C->arg_end(), std::back_inserter(args));
	  assert(F.arg_size() <= args.size());
	  const int call_varargs = args.size() - F.arg_size();
	  std::transform(Ts.begin() + call_varargs, Ts.end(), std::back_inserter(args), [] (Type *T) -> Constant * {
	    return Constant::getNullValue(T);
	  });
	  CallInst *newC = IRB.CreateCall(newF, args);
	  C->replaceAllUsesWith(newC);
	  C->eraseFromParent();
	} else {
	  errs() << "finite vararg function used by a non-call instruction: " << *user << "\n";
	  std::abort();
	}
      }

      return true;
    }

    Function *emitWrapperForFiniteVarArgFunc(Function& F, const std::vector<Type *>& Ts) {
      assert(F.isDeclaration());

      Module& M = *F.getParent();
      LLVMContext& ctx = F.getContext();

      std::vector<Type *> arg_types;

      FunctionType *T = cast<FunctionType>(F.getType()->getPointerElementType());

      // always-present arguments 
      for (Type *arg_type : T->params()) {
	arg_types.push_back(arg_type);
      }
      
      // variadic arguments
      for (Type *arg_type : Ts) {
	arg_types.push_back(arg_type);
      }

      // new function type
      FunctionType *newT = FunctionType::get(T->getReturnType(), arg_types, false); // NOTE: no longer vararg
      Function *newF = Function::Create(newT, Function::LinkageTypes::WeakAnyLinkage, wrapperName(F), M);
      BasicBlock *B = BasicBlock::Create(ctx, "", newF);
      IRBuilder<> IRB (B);

      // entering fence
      IRB.CreateCall(makeFence(ctx));

      // call to external function
      std::vector<Value *> args;
      for (Argument& A : newF->args()) {
	args.push_back(&A);
      }
      CallInst *C = IRB.CreateCall(T, &F, args);

      // exiting fence
      IRB.CreateCall(makeFence(ctx));

      // return
      if (C->getType()->isVoidTy()) {
	IRB.CreateRetVoid();
      } else {
	IRB.CreateRet(C);
      }

      return newF;
    }

    void emitWrappersForVarArgDecl(Function& F) {
      Module& M = *F.getParent();
      LLVMContext& ctx = M.getContext();
      
      // collect types
      std::map<std::vector<Type *>, std::vector<CallInst *>> sigs;
      for (User *user : F.users()) {
	if (CallInst *C = dyn_cast<CallInst>(user)) {
	  std::vector<Type *> sig;
	  for (Use& U : C->args()) {
	    sig.push_back(U.get()->getType());
	  }
	  sigs[sig].push_back(C);
	} else {
	  errs() << "CLOU: warning: calls through the following value to the variadic function \"" << F.getName()
		 << "\" will not be mitigated:\n" << *user << "\n";
	}
      }

      // generate wrapper for each type
      unsigned counter = 0;
      for (const auto& sigp : sigs) {
	const std::vector<Type *>& sig = sigp.first;
	const std::vector<CallInst *>& calls = sigp.second;

	// emit weak function definition
	FunctionType *newT = FunctionType::get(F.getReturnType(), sig, false);
	// TODO: need a different linkage if the vararg is static!
	errs() << wrapperName(F, counter) << "\n";
	Function *newF = Function::Create(newT, Function::LinkageTypes::WeakAnyLinkage, wrapperName(F, counter), M);
	BasicBlock *B = BasicBlock::Create(ctx, "", newF);
	IRBuilder<> IRB (B);

	// TODO: Not sure if we really need fences here if there are <= 6 arguments.

	// entering fence
	IRB.CreateCall(makeFence(ctx));

	// emit call
	std::vector<Value *> args;
	for (Argument& A : newF->args()) {
	  args.push_back(&A);
	}
	Instruction *C = IRB.CreateCall(&F, args);

	// exiting fence
	IRB.CreateCall(makeFence(ctx));

	// return
	if (C->getType()->isVoidTy()) {
	  IRB.CreateRetVoid();
	} else {
	  IRB.CreateRet(C);
	}

	// replace all calls w/ this signature to call our new wrapper
	for (CallInst *call : calls) {
	  call->setCalledFunction(newF);
	}
      }
    }

    #if 0
    void emitWrapperForVarArgDecl(Function& F) {
      Module& M = *F.getParent();
      
      // emit weak function definition
      FunctionType *T = cast<FunctionType>(F.getType()->getPointerElementType());
      Function *newF = Function::Create(T, Function::LinkageTypes::WeakAnyLinkage, wrapperName(F), M);
      BasicBlock *B = BasicBlock::Create(ctx, "", newF);
      IRBuilder<> IRB (B);

      // entering fence
      IRB.CreateCall(makeFence(ctx));

      // get va-list version
      auto oldFName = nonVariadicName(F);
      assert(!oldFName.empty());
      Function *nonVariadicF = M.getFunction(oldFName);
      FunctionType *nonVariadicT;
      StructType *vaListT;
      if (nonVariadicF == nullptr) {
	vaListT = StructType::create(std::vector<Type *> {
	    Type::getInt32Ty(ctx), Type::getInt32Ty(ctx), Type::getInt8PtrTy(ctx), Type::getInt8PtrTy(ctx)
	  }, "va_list");
	std::vector<Type *> types;
	std::copy(T->param_begin(), T->param_end(), std::back_inserter(types));
	types.push_back(PointerType::getUnqual(vaListT));
	nonVariadicT = FunctionType::get(T->getReturnType(), types, false);
	nonVariadicF = Function::Create(nonVariadicT, Function::LinkageTypes::ExternalLinkage, oldFName, M);
      } else {
	nonVariadicT = cast<FunctionType>(nonVariadicF->getType()->getPointerElementType());
	vaListT = cast<StructType>(nonVariadicT->params().back()->getPointerElementType());
      }

      Type *I8P = Type::getInt8PtrTy(ctx);

      // allocate stack location for va_list
      AllocaInst *vaListPtr = IRB.CreateAlloca(vaListT);

      // call to intrinsic va_start
      Value *vaListPtrI8P = IRB.CreateBitCast(vaListPtr, I8P);
      IRB.CreateIntrinsic(Intrinsic::vastart, ArrayRef<Type *>(), std::vector<Value *> {vaListPtrI8P});
      
      // call to external function
      std::vector<Value *> args;
      for (Argument& A : newF->args()) {
	args.push_back(&A);
      }
      args.push_back(vaListPtr);
      errs() << "non-variadic F:\n" << *nonVariadicF << "\n";
      errs() << "args:\n";
      for (Value *V : args) { errs() << *V << "\n"; }
      Instruction *C = IRB.CreateCall(cast<FunctionType>(nonVariadicF->getType()->getPointerElementType()), nonVariadicF, args);

      // call to intrisic va_end
      IRB.CreateIntrinsic(Intrinsic::vaend, ArrayRef<Type *>(), std::vector<Value *> {vaListPtrI8P});
      
      // exiting fence
      IRB.CreateCall(makeFence(ctx));

      // return
      if (C->getType()->isVoidTy()) {
	IRB.CreateRetVoid();
      } else {
	IRB.CreateRet(C);
      }

      replaceUses(&F, newF);
    }
#endif

    void emitAliasForDefinition(Function& F) {
      using L = GlobalAlias::LinkageTypes;
      L linkage;
      switch (F.getLinkage()) {
      case L::ExternalLinkage:
	linkage = L::ExternalLinkage;
	break;

      case L::WeakAnyLinkage:
	linkage = L::WeakAnyLinkage;
	break;
	
      case L::InternalLinkage:
	return;

      default:
	errs() << getPassName() << ": " << __FUNCTION__ << ": unhandled linkage type: " << F.getLinkage()
	       << " for function " << F.getName() << "\n";
	std::abort();
      }
      
      GlobalAlias::create(linkage, wrapperName(F), &F);
      assert(F.getParent()->getNamedAlias(wrapperName(F).str()) != nullptr);
    }

    void replaceUses(Function *oldF, Function *newF) {
      assert(newF->size() == 1);

      for (User *U : oldF->users()) {
	if (CallInst *C = dyn_cast<CallInst>(U)) {
	  if (C->getFunction() == newF) {
	    continue;
	  }
	}
	
	if (Constant *C = dyn_cast<Constant>(U)) {
	  C->handleOperandChange(oldF, newF);
	  continue;
	}
	
	U->replaceUsesOfWith(oldF, newF);
      }
      
      // oldF->replaceUsesOutsideBlock(newF, &newF->getEntryBlock());
    }
		       
  };


  const std::map<std::string, std::vector<std::function<Type * (LLVMContext&)>>> FunctionLocalStacks::std_finite_varargs = {
    {"open", {&Type::getInt32Ty}},
  };
  
}

char FunctionLocalStacks::ID = 0;

static RegisterPass<FunctionLocalStacks> X ("clou-function-local-stacks", "Clou's Function Local Stacks IR Pass", false, false);
static util::RegisterClangPass<FunctionLocalStacks> Y;

}
