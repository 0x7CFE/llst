//g++ cxx_eh.cpp `llvm-config-3.4 --cppflags --ldflags --libs all` -ldl -lffi -ltinfo -pthread -g3

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

#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ManagedStatic.h"
#include <llvm/Support/DynamicLibrary.h>
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <llvm/Analysis/Verifier.h>
#include "llvm/Transforms/Instrumentation.h"

#include <typeinfo>

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

class MCJITMemoryManager : public SectionMemoryManager
{
  MCJITMemoryManager(const MCJITMemoryManager&) LLVM_DELETED_FUNCTION;
  void operator=(const MCJITMemoryManager&) LLVM_DELETED_FUNCTION;
  ExecutionEngine* E;
public:
  MCJITMemoryManager() : SectionMemoryManager(), E(0) {}
  virtual ~MCJITMemoryManager() {}

  virtual void  notifyObjectLoaded(ExecutionEngine *EE, const ObjectImage*);
  virtual uint64_t getSymbolAddress(const std::string &Name);
};

void MCJITMemoryManager::notifyObjectLoaded(ExecutionEngine* EE, const ObjectImage*)
{
    E = EE;
}

uint64_t MCJITMemoryManager::getSymbolAddress(const std::string& Name)
{
    if (E) {
      Function* f = E->FindFunctionNamed(Name.c_str());
      if (f) {
          void* addr = E->getPointerToGlobalIfAvailable(f);
          if (addr) {
              sys::DynamicLibrary::AddSymbol(Name, addr);
              return reinterpret_cast<uint64_t>(addr);
          }
      }
    }
    return SectionMemoryManager::getSymbolAddress(Name);
}

int main() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
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

    ModulePass *DebugIRPass = createDebugIRPass(
        /*HideDebugIntrinsics*/ false, /*HideDebugMetadata*/ false,
        /*Directory*/ "./", /*Filename*/ "llst_cxx_eh");
    DebugIRPass->runOnModule(*M);

    TargetOptions Opts;
    ExecutionEngine* EE = EngineBuilder(M)
                        .setEngineKind(EngineKind::JIT)
                        .setUseMCJIT(true)
                        .setMCJITMemoryManager(new MCJITMemoryManager())
                        .setTargetOptions(Opts)
                        .create();

    //sys::DynamicLibrary::AddSymbol(throwFunc->getName(), reinterpret_cast<void*>(&throwMyStruct));
    EE->addGlobalMapping(throwFunc, reinterpret_cast<void*>(&throwMyStruct));
    sys::DynamicLibrary::AddSymbol(MyStructTypeInfo->getName(), MyStruct::getTypeInfo());

    EE->finalizeObject();
    verifyFunction(*testExceptions);
    outs() << *testExceptions;

    std::vector<GenericValue> noArgs;
    GenericValue gv = EE->runFunction(testExceptions, noArgs);

    outs() << "\ntestExceptions result: " << gv.IntVal << "\n";

    delete EE;
    llvm_shutdown();
    return 0;
}
