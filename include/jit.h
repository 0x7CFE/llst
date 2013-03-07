/*
 *    jit.h
 *
 *    LLVM related routines
 *
 *    LLST (LLVM Smalltalk or Lo Level Smalltalk) version 0.1
 *
 *    LLST is
 *        Copyright (C) 2012 by Dmitry Kashitsyn   aka Korvin aka Halt <korvin@deeptown.org>
 *        Copyright (C) 2012 by Roman Proskuryakov aka Humbug          <humbug@deeptown.org>
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

#include <llvm/Function.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Linker.h>
#include "llvm/Support/raw_ostream.h"
#include <llvm/PassManager.h>
#include <llvm/Intrinsics.h>

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
    llvm::GlobalValue* binarySelectors[3];

    void initializeFromModule(llvm::Module* module) {
        nilObject          = module->getGlobalVariable("globals.nilObject");
        trueObject         = module->getGlobalVariable("globals.trueObject");
        falseObject        = module->getGlobalVariable("globals.falseObject");
        smallIntClass      = module->getGlobalVariable("globals.smallIntClass");
        arrayClass         = module->getGlobalVariable("globals.arrayClass");
        binarySelectors[0] = module->getGlobalVariable("globals.<");
        binarySelectors[1] = module->getGlobalVariable("globals.<=");
        binarySelectors[2] = module->getGlobalVariable("globals.+");

    //badMethodSymbol =
    }
};

struct TBaseFunctions {
    llvm::Function* isSmallInteger;
    llvm::Function* getIntegerValue;
    llvm::Function* newInteger;
    llvm::Function* TObject__getSize;
    llvm::Function* TObject__getClass;
    llvm::Function* TObject__getFields;
    llvm::Function* TObject__getField;
    llvm::Function* TObject__setField;
    llvm::Function* getSlotSize;

    void initializeFromModule(llvm::Module* module) {
        isSmallInteger     = module->getFunction("isSmallInteger()");
        getIntegerValue    = module->getFunction("getIntegerValue()");
        newInteger         = module->getFunction("newInteger()");
        TObject__getSize   = module->getFunction("TObject::getSize()");
        TObject__getClass  = module->getFunction("TObject::getClass()");
        TObject__getFields = module->getFunction("TObject::getFields()");
        TObject__getField  = module->getFunction("getObjectField");
        TObject__setField  = module->getFunction("setObjectField");
        getSlotSize        = module->getFunction("getSlotSize()");
    }
};

class TStackValue {
public:
    virtual ~TStackValue() { }
    virtual llvm::Value* get() = 0;
};

class TPlainValue : public TStackValue {
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
        TContext*           callingContext;
        uint32_t            bytePointer;
        uint32_t            byteCount;

        llvm::Function*     function;     // LLVM function that is created based on method
        TInstruction        instruction;  // currently processed instruction
        llvm::IRBuilder<>*  builder;      // Builder inserts instructions into basic blocks

        llvm::BasicBlock*   preamble;
        llvm::BasicBlock*   exceptionLandingPad;
        bool                methodHasBlockReturn;

        typedef std::map<llvm::BasicBlock*, TBasicBlockContext> TBlockContextMap;
        TBlockContextMap basicBlockContexts;

        // Value stack is used as a FIFO value holder during the compilation process.
        // Software VM uses object arrays to hold the values in dynamic.
        // Instead we're interpriting the push, pop and assign instructions
        // as a commands which values should be linked together. For example,
        // two subsequent instructions 'pushTemporary 1' and 'assignInstance 2'
        // will be linked together with effect of instanceVariables[2] = temporaries[1]

        MethodCompiler* compiler; // link to outer class for variable access
        bool hasValue();
        void pushValue(llvm::Value* value); 
        llvm::Value* lastValue(); 
        llvm::Value* popValue(llvm::BasicBlock* overrideBlock = 0);

        llvm::Value* contextHolder;
        llvm::Value* selfHolder;
        
        llvm::Value* getCurrentContext();
        llvm::Value* getSelf();
        llvm::Value* getMethodClass();
        llvm::Value* getLiteral(uint32_t index);
        
        void pushValue(TStackValue* value);
        
        TJITContext(MethodCompiler* compiler, TMethod* method, TContext* context)
            : method(method), callingContext(context),
            bytePointer(0), function(0), builder(0), 
            preamble(0), exceptionLandingPad(0), methodHasBlockReturn(false), compiler(compiler),
            contextHolder(0), selfHolder(0)
        {
            byteCount = method->byteCodes->getSize();
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
            
            if (builder) 
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
    void doPushBlock(uint32_t currentOffset, TJITContext& jit);
    void doAssignTemporary(TJITContext& jit);
    void doAssignInstance(TJITContext& jit);
    void doMarkArguments(TJITContext& jit);
    void doSendUnary(TJITContext& jit);
    void doSendBinary(TJITContext& jit);
    void doSendMessage(TJITContext& jit);
    void doSpecial(TJITContext& jit);
    void doPrimitive(TJITContext& jit);

    void printOpcode(TInstruction instruction);

    llvm::Value*    createArray(TJITContext& jit, uint32_t elementsCount);
    llvm::Function* createFunction(TMethod* method);
public:
    llvm::Function* compileMethod(TMethod* method, TContext* callingContext);

    MethodCompiler(
        llvm::Module* JITModule,
        TRuntimeAPI   runtimeApi,
        TExceptionAPI exceptionApi
    )
        : m_JITModule(JITModule),
        m_runtimeAPI(runtimeApi), m_exceptionAPI(exceptionApi)
    {
        m_baseTypes.initializeFromModule(JITModule);
        m_globals.initializeFromModule(JITModule);
        m_baseFunctions.initializeFromModule(JITModule);
    }
};

class TDeferredValue : public TStackValue {
public:
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
    TObject*     sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass);
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

    TObject* sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass);
    TBlock*  createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer);
    TObject* invokeBlock(TBlock* block, TContext* callingContext);

    friend TObject*     newOrdinaryObject(TClass* klass, uint32_t slotSize);
    friend TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize);
    friend TObject*     sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass);
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

