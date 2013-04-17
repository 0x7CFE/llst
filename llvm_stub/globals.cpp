//g++ globals.cpp `llvm-config-3.0 --cppflags --ldflags --libs all` -ldl -lffi

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

struct TGlobals {
    int x;
    int y;
};

TGlobals tGlobals; // tGlobals

Module *M;

void createGlobals() {
    //First of all, lets init tGlobals
    tGlobals.x = 42;
    tGlobals.y = 23;
    
    LLVMContext& context = M->getContext();
    StructType* globalsType = StructType::create(context, "struct.TGlobals");
    Type* globalsTypeFields[] = {
        IntegerType::get(context, 32), //x
        IntegerType::get(context, 32)  //y
    };
    globalsType->setBody(globalsTypeFields, false);
    
    
    GlobalVariable* globals = cast<GlobalVariable>( M->getOrInsertGlobal("globals", globalsType) );
}

int main() {
    InitializeNativeTarget();
    LLVMContext& Context = getGlobalContext();
    M = new Module("test", Context);
  
    createGlobals();
  
    Function* testGlobals = cast<Function>(M->getOrInsertFunction("testGlobals", Type::getInt32Ty(Context), NULL));

    BasicBlock *BB = BasicBlock::Create(Context, "", testGlobals);
    IRBuilder<> builder(BB);
    
    GlobalValue* globals = M->getGlobalVariable("globals");
    
    Value* globalsX = builder.CreateStructGEP(globals, 0);
    Value* xValue   = builder.CreateLoad(globalsX);
    builder.CreateRet(xValue);

    ExecutionEngine* EE = EngineBuilder(M).create();
    
    // !!!! map globals
    EE->addGlobalMapping(globals, reinterpret_cast<void*>(&tGlobals));

    outs() << "We just constructed this LLVM module:\n" << *M;
    outs() << "\nRunning testGlobals... ";
    outs().flush();

    // Call the testGlobals function with no arguments:
    std::vector<GenericValue> noArgs;
    GenericValue gv = EE->runFunction(testGlobals, noArgs);

    // Import result of execution:
    outs() << "Result: " << gv.IntVal << "\n";
    EE->freeMachineCodeForFunction(testGlobals);
    delete EE;
    llvm_shutdown();
    return 0;
}
