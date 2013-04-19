// g++ call_extern_member_function.cpp `llvm-config-3.1 --cppflags --ldflags --libs all` -ldl

#include <string>
#include <stdio.h>

class MyClass {
    std::string m_moduleName;
    public:
    MyClass(std::string moduleName) : m_moduleName(moduleName) {}
    void printSomething(int x) {
        printf("Hello from module '%s' with x '%d'\n", m_moduleName.c_str(), x);
    }
};

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include <llvm/Analysis/Verifier.h>

using namespace llvm;

Module* buildModule(LLVMContext& Context) {
    /*    extern void* s_myObj; // s_myObj is an instance of MyClass
     *    extern void printSomething(void* this, int x); // void MyClass::printSomething(int x);
     *    void main() {
     *       printSomething(s_myObj, 42);
     *    }
     */
    
    Module* module = new Module("Call extern member function", Context);
    
    Constant* s_myObj = module->getOrInsertGlobal("s_myObj", Type::getInt8PtrTy(Context));
    
    //let's link MyClass::printSomething()
    Type* printSomethingParams[] = {
        Type::getInt8PtrTy(Context)->getPointerTo(), // this
        Type::getInt32Ty(Context) // x
    };
    FunctionType* printSomethingType = FunctionType::get(Type::getVoidTy(Context), printSomethingParams, false);
    Constant* printSomething = module->getOrInsertFunction("printSomething", printSomethingType);
    
    //let's build main
    Function* main = cast<Function>(module->getOrInsertFunction("main", Type::getVoidTy(Context), NULL));
    BasicBlock* entry = BasicBlock::Create(Context, "entry", main);
    IRBuilder<> builder(entry);
    builder.CreateCall2(printSomething, s_myObj, builder.getInt32(42));
    builder.CreateRetVoid();
    
    return module;
}

int main() {
    InitializeNativeTarget();
    LLVMContext& Context = getGlobalContext();
    Module* module = buildModule(Context);
    
    ExecutionEngine* EE = EngineBuilder(module)
                        .setEngineKind(EngineKind::JIT)
                        .create();
    
    Constant* s_myObj = module->getGlobalVariable("s_myObj");
    MyClass* myObj = new MyClass("Hello world");
    EE->addGlobalMapping(cast<GlobalValue>(s_myObj), reinterpret_cast<void*>(myObj));
    
    Function* printSomething = module->getFunction("printSomething");
    EE->addGlobalMapping(cast<GlobalValue>(printSomething), reinterpret_cast<void*>(&MyClass::printSomething));
    
    outs() << *module;
    verifyModule(*module);
    
    Function* mainF = module->getFunction("main");
    std::vector<GenericValue> args;
    EE->runFunction(mainF, args);
    
    delete module;
    delete myObj;
    return 0;
}