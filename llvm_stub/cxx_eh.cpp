//g++ cxx_eh.cpp `llvm-config-3.1 --cppflags --ldflags --libs all` -ldl -lffi

/* assume we have C++ code:
 *
 * struct MyStruct {
 *    int x;
 *    int y;
 *    MyStruct(int _x, int _y) : x(_y), y(_y) { }
 * };
 *
 * int main() {
 *    try {
 *        throw MyStruct(3, 42);
 *        return 666;
 *    } catch(MyStruct z) {
 *        return z.y;
 *    }
 * }
 *
 * We want to implement the same using llvm!
 */

//TODO call void @__cxa_free_exception(i8*)

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Intrinsics.h"

#include "llvm/Analysis/Verifier.h"

#include <typeinfo> // remove from release

using namespace llvm;

struct MyStruct {
    int x;
    int y;
    MyStruct(int _x, int _y) : x(_y), y(_y) { }

    static void* getTypeInfo() {
        //this function should be removed because we are compilling with -no-rtti
        return const_cast<void*>(
                reinterpret_cast<const void*>(
                    &typeid(MyStruct)
                )
            );
    }
};

extern "C" {
    void throwMyStruct() {
        throw MyStruct(3,42);
    }
}

int main() {
    InitializeNativeTarget();
    LLVMContext& Context = getGlobalContext();
    Module *M = new Module("test C++ exception handling ", Context);


    StructType* MyStructType = StructType::create(Context, "struct.MyStruct");
    Type* MyStructFields[] = {
        Type::getInt32Ty(Context),
        Type::getInt32Ty(Context)
    };
    MyStructType->setBody(MyStructFields);

    GlobalValue* throwFunc = cast<GlobalValue>(M->getOrInsertFunction("throwMyStruct", Type::getVoidTy(Context), NULL));
    GlobalValue* MyStructTypeInfo = cast<GlobalValue>(M->getOrInsertGlobal("MyStructTypeInfo", Type::getInt8Ty(Context)));

    Function* gxx_personality = Function::Create(FunctionType::get(Type::getInt32Ty(Context), true), Function::ExternalLinkage, "__gxx_personality_v0", M);
    Function* begin_catch     = Function::Create(FunctionType::get(Type::getInt8PtrTy(Context), Type::getInt8PtrTy(Context), false), Function::ExternalLinkage, "__cxa_begin_catch", M);
    Function* end_catch       = Function::Create(FunctionType::get(Type::getVoidTy(Context), false), Function::ExternalLinkage, "__cxa_end_catch", M);

    Function* testExceptions = cast<Function>(M->getOrInsertFunction("testExceptions", Type::getInt32Ty(Context), NULL));

    BasicBlock* entryBB   = BasicBlock::Create(Context, "", testExceptions);
    BasicBlock* landPadBB = BasicBlock::Create(Context, "landPad", testExceptions);
    BasicBlock* noErrorBB = BasicBlock::Create(Context, "noError", testExceptions);


    IRBuilder<> builder(entryBB);

    Value* invokeThrow = builder.CreateInvoke(throwFunc, noErrorBB, landPadBB);

    builder.SetInsertPoint(noErrorBB);
    builder.CreateRet( builder.getInt32(666) ); // should never happen

    //writing landingpad! <<<<<<<

    builder.SetInsertPoint(landPadBB);

    Value* gxx_personality_i8 = builder.CreateBitCast(gxx_personality, Type::getInt8PtrTy(Context));
    Type* caughtType = StructType::get(builder.getInt8PtrTy(), builder.getInt32Ty(), NULL);

    LandingPadInst* caughtResult = builder.CreateLandingPad(caughtType, gxx_personality_i8, 1);

    // we can catch any C++ exception we want
    // but now we are catching MyStruct
    caughtResult->addClause(MyStructTypeInfo);

    //we are sure to catch MyStruct so no other checks are needed
    //if throwMyStruct() throws anything but MyStruct it won't pass to the current landingpad BB

    Value* thrownExctn  = builder.CreateExtractValue(caughtResult, 0);
    Value* thrownObject = builder.CreateCall(begin_catch, thrownExctn);
    Value* object       = builder.CreateBitCast(thrownObject, MyStructType->getPointerTo());
    Value* resultPtr    = builder.CreateStructGEP(object, 1);
    Value* result       = builder.CreateLoad(resultPtr);

    builder.CreateCall(end_catch);

    builder.CreateRet( result ); // << z.y

    TargetOptions Opts;
    Opts.JITExceptionHandling = true; // DO NOT FORGET THIS OPTION !!!!!!11


    ExecutionEngine* EE = EngineBuilder(M)
                        .setEngineKind(EngineKind::JIT)
                        .setTargetOptions(Opts)
                        .create();

    EE->addGlobalMapping(throwFunc, reinterpret_cast<void*>(&throwMyStruct));
    EE->addGlobalMapping(MyStructTypeInfo, MyStruct::getTypeInfo());

    verifyFunction(*testExceptions);
    outs() << *testExceptions;

    std::vector<GenericValue> noArgs;
    GenericValue gv = EE->runFunction(testExceptions, noArgs);

    outs() << "\ntestExceptions result: " << gv.IntVal << "\n";

    delete EE;
    llvm_shutdown();
    return 0;
}