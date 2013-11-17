/*
g++ changeFunctinOnTheFly.cpp `llvm-config-3.0 --cxxflags --libs ` -lrt -ldl -lpthread
*/

#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm;

ExecutionEngine* EE; // JIT execution engine

extern "C" void changeCallee() {
    outs() << "changeCallee was called\n\n";

    Function* CalleeFunction = EE->FindFunctionNamed("CalleeFunction");

    CalleeFunction->deleteBody();

    LLVMContext& Context = getGlobalContext();
    BasicBlock *BB = BasicBlock::Create(Context, "", CalleeFunction);

    IRBuilder<> builder(BB);

    Value* Any = builder.getInt32(42);
    builder.CreateRet(Any);

    EE->recompileAndRelinkFunction(CalleeFunction);
}


int main() {
    InitializeNativeTarget();

    LLVMContext& Context = getGlobalContext();
    Module* M = new Module("test", Context);

    // Create CalleeFunction
    Function* CalleeFunction = cast<Function>( M->getOrInsertFunction("CalleeFunction", Type::getInt32Ty(Context), (Type*)0) );

    BasicBlock *BB = BasicBlock::Create(Context, "", CalleeFunction);

    IRBuilder<> builder(BB);

    Value* Any = builder.getInt32(23);
    builder.CreateRet(Any);

    //Create CallerFunction
    Function* CallerFunction = cast<Function>( M->getOrInsertFunction("CallerFunction", Type::getInt32Ty(Context), (Type*)0) );

    BB = BasicBlock::Create(Context, "", CallerFunction);

    builder.SetInsertPoint(BB);

    builder.CreateCall(CalleeFunction);

    Function* ChangeCalleeFunction = cast<Function>( M->getOrInsertFunction("changeCallee", Type::getVoidTy(Context), (Type*)0) );
    ChangeCalleeFunction->setLinkage(Function::ExternalLinkage);

    builder.CreateCall(ChangeCalleeFunction);

    Value* CalleeResult = builder.CreateCall(CalleeFunction);

    builder.CreateRet(CalleeResult);

    //Call CallerFunction

    outs() << "Before call: \n" << *M  << "\n";


    EE = ExecutionEngine::createJIT(M);
    EE->addGlobalMapping(ChangeCalleeFunction, reinterpret_cast<void*>(&changeCallee));

    std::vector<GenericValue> NoArgs;
    GenericValue JITResult = EE->runFunction(CallerFunction, NoArgs);

    outs() << "After call: \n" << *M  << "\n";
    outs() << "JIT result: " << JITResult.IntVal  << "\n";

    EE->freeMachineCodeForFunction(CalleeFunction);
    EE->freeMachineCodeForFunction(CallerFunction);

    return 0;
}