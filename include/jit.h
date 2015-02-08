/*
 *    jit.h
 *
 *    LLVM related routines
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.3
 *
 *    LLST is
 *        Copyright (C) 2012-2015 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2015 by Roman Proskuryakov <humbug@deeptown.org>
 *
 *    LLST is based on the LittleSmalltalk which is
 *        Copyright (C) 1987-2005 by Timothy A. Budd
 *        Copyright (C) 2007 by Charles R. Childers
 *        Copyright (C) 2005-2007 by Danny Reinhold
 *
 *    Original license of LittleSmalltalk may be found in the LICENSE file.
 *
 *
 *    This file is part of LLST.
 *    LLST is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    LLST is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with LLST.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <types.h>
#include "vm.h"

#include <typeinfo>

#include <map>
#include <list>
#include <set>
#include <stdio.h>

#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/PassManager.h>

// These functions are used in the IR code
// as a bindings between the VM and the object world
// TJITRuntime provides this API to the MethodCompiler
// which in turn uses it to build calls in functions
struct TRuntimeAPI {
    llvm::Function* newOrdinaryObject;
    llvm::Function* newBinaryObject;
    llvm::Function* sendMessage;
    llvm::Function* createBlock;
    llvm::Function* invokeBlock;
    llvm::Function* emitBlockReturn;
    llvm::Function* checkRoot;
    llvm::Function* callPrimitive;
    llvm::Function* bulkReplace;
};

struct TExceptionAPI {
    llvm::Function*    gcc_personality;
    llvm::Function*    cxa_begin_catch;
    llvm::Function*    cxa_end_catch;
    llvm::Function*    cxa_allocate_exception;
    llvm::Function*    cxa_throw;
    llvm::GlobalValue* blockReturnType;
    llvm::GlobalValue* contextTypeInfo;
};

struct TObjectTypes {
    llvm::StructType* object;
    llvm::StructType* klass;
    llvm::StructType* context;
    llvm::StructType* block;
    llvm::StructType* dictionary;
    llvm::StructType* method;
    llvm::StructType* symbol;
    llvm::StructType* objectArray;
    llvm::StructType* symbolArray;
    llvm::StructType* globals;
    llvm::StructType* byteObject;
    llvm::StructType* blockReturn;
    llvm::StructType* process;


    void initializeFromModule(llvm::Module* module) {
        object      = module->getTypeByName("TObject");
        klass       = module->getTypeByName("TClass");
        context     = module->getTypeByName("TContext");
        block       = module->getTypeByName("TBlock");
        dictionary  = module->getTypeByName("TDictionary");
        method      = module->getTypeByName("TMethod");
        symbol      = module->getTypeByName("TSymbol");
        objectArray = module->getTypeByName("TObjectArray");
        symbolArray = module->getTypeByName("TSymbolArray");
        globals     = module->getTypeByName("TGlobals");
        byteObject  = module->getTypeByName("TByteObject");
        blockReturn = module->getTypeByName("TBlockReturn");
        process     = module->getTypeByName("TProcess");
    }
};

struct TJITGlobals {
    llvm::GlobalValue* nilObject;
    llvm::GlobalValue* trueObject;
    llvm::GlobalValue* falseObject;
    llvm::GlobalValue* smallIntClass;
    llvm::GlobalValue* arrayClass;
    llvm::GlobalValue* contextClass;
    llvm::GlobalValue* binarySelectors[3];

    void initializeFromModule(llvm::Module* module) {
        nilObject          = module->getGlobalVariable("nilObject");
        trueObject         = module->getGlobalVariable("trueObject");
        falseObject        = module->getGlobalVariable("falseObject");
        smallIntClass      = module->getGlobalVariable("SmallInt");
        arrayClass         = module->getGlobalVariable("Array");
        contextClass       = module->getGlobalVariable("Context");
        binarySelectors[0] = module->getGlobalVariable("<");
        binarySelectors[1] = module->getGlobalVariable("<=");
        binarySelectors[2] = module->getGlobalVariable("+");

    //badMethodSymbol =
    }
};

struct TBaseFunctions {
    llvm::Function* isSmallInteger;
    llvm::Function* getIntegerValue;
    llvm::Function* newInteger;
    llvm::Function* getObjectSize;
    llvm::Function* setObjectSize;
    llvm::Function* getObjectClass;
    llvm::Function* setObjectClass;
    llvm::Function* getObjectFields;
    llvm::Function* getObjectField;
    llvm::Function* setObjectField;
    llvm::Function* getSlotSize;
    llvm::Function* getLiteral;

    void initializeFromModule(llvm::Module* module) {
        isSmallInteger   = module->getFunction("isSmallInteger");
        getIntegerValue  = module->getFunction("getIntegerValue");
        newInteger       = module->getFunction("newInteger");
        getObjectSize    = module->getFunction("getObjectSize");
        setObjectSize    = module->getFunction("setObjectSize");
        getObjectClass   = module->getFunction("getObjectClass");
        setObjectClass   = module->getFunction("setObjectClass");
        getObjectFields  = module->getFunction("getObjectFields");
        getObjectField   = module->getFunction("getObjectField");
        setObjectField   = module->getFunction("setObjectField");
        getSlotSize      = module->getFunction("getSlotSize");
        getLiteral       = module->getFunction("getLiteralFromContext");
    }
};

// Value stack is used as a FIFO value holder during the compilation process.
// Software VM uses object arrays to hold the values in dynamic.
// Instead we're interpriting the push, pop and assign instructions
// as a commands which values should be linked together. For example,
// two subsequent instructions 'pushTemporary 1' and 'assignInstance 2'
// will be linked together with effect of instanceVariables[2] = temporaries[1]
//
// TStackValue is a base class for stack values. Descendant value may contain either
// raw value or a deferred command to be done when evaluation takes place.
// Use get() method to get the stored value.
//
// NOTE: Depending on the actual implementation, invoking method get() may result in
//       a code generation. This should be done in a known context.
//
// See also: TPlainValue and TDeferredValue
struct TStackValue {
    virtual ~TStackValue() { }
    virtual llvm::Value* get() = 0;
};

// TPlainValue represents simple llvm::Value* holder which does not
// perform any additional actions when get() is called. Only stored value is returned.
struct TPlainValue : public TStackValue {
private:
    llvm::Value* m_value;
public:
    TPlainValue(llvm::Value* value) : m_value(value) {}
    virtual ~TPlainValue() { }

    virtual llvm::Value* get() { return m_value; }
};

class MethodCompiler {
public:
    // Some useful type aliases
    typedef std::list<TStackValue*> TValueStack;
    typedef std::set<llvm::BasicBlock*> TRefererSet;

    // Block context is a logic encapsulation
    // of Smalltalk's CFG and value transitions
    struct TBasicBlockContext {
        // Compile time stack of values that are
        // produced as a result of opcode processing
        TValueStack valueStack;

        // Blocks that are referencing
        // current block by branching to it
        TRefererSet referers;
    };

    // This structure contains working data which is
    // used during the compilation process.
    struct TJITContext {
        TMethod*            method;       // Smalltalk method we're currently processing
        uint32_t            bytePointer;

        llvm::Function*     function;     // LLVM function that is created based on method
        TInstruction        instruction;  // currently processed instruction
        llvm::IRBuilder<>*  builder;      // Builder inserts instructions into basic blocks

        llvm::BasicBlock*   preamble;
        llvm::BasicBlock*   exceptionLandingPad;
        bool                methodHasBlockReturn;

        typedef std::map<llvm::BasicBlock*, TBasicBlockContext> TBlockContextMap;
        TBlockContextMap basicBlockContexts;

        MethodCompiler* compiler; // link to outer class for variable access
        bool hasValue();
        void pushValue(llvm::Value* value);
        llvm::Value* lastValue();
        llvm::Value* popValue(llvm::BasicBlock* overrideBlock = 0, bool dropValue = false);

        llvm::Value* contextHolder;
        llvm::Value* selfHolder;

        llvm::Value* getCurrentContext();
        llvm::Value* getSelf();
        llvm::Value* getMethodClass();
        llvm::Value* getLiteral(uint32_t index);

        void pushValue(TStackValue* value);

        TJITContext(MethodCompiler* compiler, TMethod* method)
            : method(method), bytePointer(0), function(0), builder(0),
            preamble(0), exceptionLandingPad(0), methodHasBlockReturn(false), compiler(compiler),
            contextHolder(0), selfHolder(0)
        {

        };

        ~TJITContext() {
            if (! basicBlockContexts.empty()) {
                TBlockContextMap::iterator iContext = basicBlockContexts.begin();
                while (iContext != basicBlockContexts.end()) {
                    TValueStack& valueStack = iContext->second.valueStack;
                    if (! valueStack.empty()) {
                        TValueStack::iterator iStackValue = valueStack.begin();
                        while (iStackValue != valueStack.end()) {
                            delete *iStackValue;
                            ++iStackValue;
                        }
                        valueStack.clear();
                    }
                    ++iContext;
                }
                basicBlockContexts.clear();
            }

            delete builder;
        }
    };

private:
    llvm::Module* m_JITModule;
    std::map<uint32_t, llvm::BasicBlock*> m_targetToBlockMap;
    void scanForBranches(TJITContext& jit, uint32_t byteCount = 0);
    bool scanForBlockReturn(TJITContext& jit, uint32_t byteCount = 0);

    std::map<std::string, llvm::Function*> m_blockFunctions;

    TObjectTypes   m_baseTypes;
    TJITGlobals    m_globals;
    TRuntimeAPI    m_runtimeAPI;
    TExceptionAPI  m_exceptionAPI;
    TBaseFunctions m_baseFunctions;

    llvm::Value* allocateRoot(TJITContext& jit, llvm::Type* type);
    llvm::Value* protectPointer(TJITContext& jit, llvm::Value* value);

    void writePreamble(TJITContext& jit, bool isBlock = false);
    void writeFunctionBody(TJITContext& jit, uint32_t byteCount = 0);
    void writeLandingPad(TJITContext& jit);

    void doPushInstance(TJITContext& jit);
    void doPushArgument(TJITContext& jit);
    void doPushTemporary(TJITContext& jit);
    void doPushLiteral(TJITContext& jit);
    void doPushConstant(TJITContext& jit);
    void doPushBlock(TJITContext& jit);
    void doAssignTemporary(TJITContext& jit);
    void doAssignInstance(TJITContext& jit);
    void doMarkArguments(TJITContext& jit);
    void doSendUnary(TJITContext& jit);
    void doSendBinary(TJITContext& jit);
    void doSendMessage(TJITContext& jit);
    void doSpecial(TJITContext& jit);

    void doPrimitive(TJITContext& jit);
    void compilePrimitive(TJITContext& jit,
                            uint8_t opcode,
                            llvm::Value*& primitiveResult, // %TObject*
                            llvm::Value*& primitiveFailed, // i1
                            llvm::BasicBlock* primitiveSucceededBB,
                            llvm::BasicBlock* primitiveFailedBB);
    void compileSmallIntPrimitive(TJITContext& jit,
                                uint8_t /*primitive::SmallIntOpcode*/ opcode,
                                llvm::Value* leftObject,
                                llvm::Value* rightObject,
                                llvm::Value*& primitiveResult,
                                llvm::BasicBlock* primitiveFailedBB);

    llvm::Value*    createArray(TJITContext& jit, uint32_t elementsCount);
    llvm::Function* createFunction(TMethod* method);

    uint32_t m_callSiteIndex;
    std::map<uint32_t, uint32_t> m_callSiteIndexToOffset;
public:
    uint32_t getCallSiteOffset(const uint32_t index) { return m_callSiteIndexToOffset[index]; }
    TBaseFunctions& getBaseFunctions() { return m_baseFunctions; }
    TRuntimeAPI& getRuntimeAPI() { return m_runtimeAPI; }
    TJITGlobals& getJitGlobals() { return m_globals; }
    TObjectTypes& getBaseTypes() { return m_baseTypes; }

    llvm::Function* compileMethod(
        TMethod* method,
        llvm::Function* methodFunction = 0,
        llvm::Value** contextHolder = 0
    );

    // TStackObject is a pair of entities allocated on a thread stack space
    // objectSlot is a container for actual object's data
    // objectHolder is a pointer to the objectSlot which is registered in GC roots
    // thus allowing GC to update the fields in object when collection takes place
    struct TStackObject {
        llvm::AllocaInst* objectSlot;
        llvm::AllocaInst* objectHolder;
    };

    TStackObject allocateStackObject(llvm::IRBuilder<>& builder, uint32_t baseSize, uint32_t fieldsCount);

    MethodCompiler(
        llvm::Module* JITModule,
        TRuntimeAPI   runtimeApi,
        TExceptionAPI exceptionApi
    )
        : m_JITModule(JITModule),
        m_runtimeAPI(runtimeApi), m_exceptionAPI(exceptionApi), m_callSiteIndex(1)
    {
        m_baseTypes.initializeFromModule(JITModule);
        m_globals.initializeFromModule(JITModule);
        m_baseFunctions.initializeFromModule(JITModule);
    }
};

// TDeferredValue is used in cases when some particular actions should be done
// when get() method is invoked. Typically this is used to pass an llvm Value*
// to the later code ensuring that it will not be broken by a garbage collection.
struct TDeferredValue : public TStackValue {
    enum TOperation {
        loadInstance,
        loadArgument,
        loadTemporary,
        loadLiteral,

        // result of message sent
        // or pushed block
        loadHolder
    };

private:
    TOperation   m_operation;
    uint32_t     m_index;
    llvm::Value* m_argument;

    MethodCompiler::TJITContext* m_jit;
public:
    TDeferredValue(MethodCompiler::TJITContext* jit, TOperation operation, uint32_t index) {
        m_argument = 0;
        m_operation = operation;
        m_index = index;
        m_jit = jit;
    }

    TDeferredValue(MethodCompiler::TJITContext* jit, TOperation operation, llvm::Value* argument) {
        m_operation = operation;
        m_argument = argument;
        m_index = 0;
        m_jit = jit;
    }

    virtual ~TDeferredValue() { }
    virtual llvm::Value* get();
};


extern "C" {
    TObject*     newOrdinaryObject(TClass* klass, uint32_t slotSize);
    TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize);
    TObject*     sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass, uint32_t callSiteIndex);
    TBlock*      createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer);
    TObject*     invokeBlock(TBlock* block, TContext* callingContext);
    void         emitBlockReturn(TObject* value, TContext* targetContext);
    const void*  getBlockReturnType();
    void         checkRoot(TObject* value, TObject** objectSlot);

    bool         bulkReplace(TObject* destination,
                            TObject* destinationStartOffset,
                            TObject* destinationStopOffset,
                            TObject* source,
                            TObject* sourceStartOffset);
}

class JITRuntime {
public:
    typedef TObject* (*TMethodFunction)(TContext*);
    typedef TObject* (*TBlockFunction)(TBlock*);

private:
    llvm::FunctionPassManager* m_functionPassManager;
    llvm::PassManager*         m_modulePassManager;

    SmalltalkVM* m_softVM;
    llvm::ExecutionEngine* m_executionEngine;
    MethodCompiler* m_methodCompiler;

    llvm::Module* m_JITModule;

    TRuntimeAPI   m_runtimeAPI;
    TExceptionAPI m_exceptionAPI;
    TObjectTypes  m_baseTypes;

    static JITRuntime* s_instance;

    TObject* sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass, uint32_t callSiteIndex = 0);

    TBlock*  createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer);
    TObject* invokeBlock(TBlock* block, TContext* callingContext);

    friend TObject*     newOrdinaryObject(TClass* klass, uint32_t slotSize);
    friend TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize);
    friend TObject*     sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass, uint32_t callSiteIndex);
    friend TBlock*      createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer);
    friend TObject*     invokeBlock(TBlock* block, TContext* callingContext);

    struct TFunctionCacheEntry
    {
        TMethod* method;
        TMethodFunction function;
    };

    struct TBlockFunctionCacheEntry
    {
        TMethod* containerMethod;
        uint32_t blockOffset;

        TBlockFunction function;
    };

    static const unsigned int LOOKUP_CACHE_SIZE = 512;
    TFunctionCacheEntry      m_functionLookupCache[LOOKUP_CACHE_SIZE];
    TBlockFunctionCacheEntry m_blockFunctionLookupCache[LOOKUP_CACHE_SIZE];

    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;
    uint32_t m_blockCacheHits;
    uint32_t m_blockCacheMisses;
    uint32_t m_messagesDispatched;
    uint32_t m_blocksInvoked;
    uint32_t m_objectsAllocated;

    TMethodFunction lookupFunctionInCache(TMethod* method);
    TBlockFunction  lookupBlockFunctionInCache(TMethod* containerMethod, uint32_t blockOffset);
    void updateFunctionCache(TMethod* method, TMethodFunction function);
    void updateBlockFunctionCache(TMethod* containerMethod, uint32_t blockOffset, TBlockFunction function);

    void initializePassManager();

    //The following methods use m_baseTypes. Don't forget to init it before calling these methods
    void initializeGlobals();
    void initializeRuntimeAPI();
    void initializeExceptionAPI();
    void createExecuteProcessFunction();

public:
    struct TCallSite {
        typedef std::map<TClass*, uint32_t> TClassHitsMap;

        uint32_t hitCount;
        TSymbol* messageSelector;
        TClassHitsMap classHits;
        TCallSite() : hitCount(0), messageSelector(0) {}
    };

    struct THotMethod {
        typedef std::map<uint32_t, TCallSite> TCallSiteMap;

        bool processed;
        uint32_t hitCount;
        TMethod* method;
        llvm::Function* methodFunction;
        TCallSiteMap callSites;

        THotMethod() : processed(false), hitCount(0), method(0), methodFunction(0) {}
    };

    struct TDirectBlock {
        llvm::BasicBlock* basicBlock;
        llvm::Value* returnValue;

        llvm::Value* contextHolder;
        llvm::Value* tempsHolder;
    };

    struct TPatchInfo {
        llvm::Instruction* callInstruction;
        llvm::Value* messageArguments;
        llvm::BasicBlock* nextBlock;
        llvm::Value* contextHolder;
    };

    typedef std::map<TMethodFunction, THotMethod> THotMethodsMap;
    typedef std::map<TClass*, TDirectBlock> TDirectBlockMap;

private:
    THotMethodsMap m_hotMethods;
    void updateHotSites(TMethodFunction methodFunction, TContext* callingContext, TSymbol* message, TClass* receiverClass, uint32_t callSiteIndex);
    void patchCallSite(llvm::Function* methodFunction, llvm::Value* contextHolder, TCallSite& callSite, uint32_t callSiteIndex);
    llvm::Instruction* findCallInstruction(llvm::Function* methodFunction, uint32_t callSiteIndex);
    void createDirectBlocks(TPatchInfo& info, TCallSite& callSite, TDirectBlockMap& directBlocks);
    void cleanupDirectHolders(llvm::IRBuilder<>& builder, TDirectBlock& directBlock);
    bool detectLiteralReceiver(llvm::Value* messageArguments);
public:
    void patchHotMethods();
    void printMethod(TMethod* method) {
        std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
        llvm::Function* methodFunction = m_JITModule->getFunction(functionName);

        if (!methodFunction)
            llvm::outs() << "Compiled method " << functionName << " is not found\n";
        else
            llvm::outs() << *methodFunction;
    }
    static JITRuntime* Instance() { return s_instance; }

    MethodCompiler* getCompiler() { return m_methodCompiler; }
    SmalltalkVM* getVM() { return m_softVM; }
    llvm::ExecutionEngine* getExecutionEngine() { return m_executionEngine; }
    llvm::Module* getModule() { return m_JITModule; }

    void optimizeFunction(llvm::Function* function);
    void printStat();

    void initialize(SmalltalkVM* softVM);
    ~JITRuntime();
};

struct TBlockReturn {
    TObject*  value;
    TContext* targetContext;
    TBlockReturn(TObject* value, TContext* targetContext)
        : value(value), targetContext(targetContext) { }

    static void* getBlockReturnType() {
        return const_cast<void*>(reinterpret_cast<const void*>( &typeid(TBlockReturn) ));
    }
};
