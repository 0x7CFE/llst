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

// These functions are used in the IR code
// as a bindings between the VM and the object world
// TJITRuntime provides this API to the MethodCompiler
// which in turn uses it to build calls in functions
struct TRuntimeAPI {
    llvm::Function* newOrdinaryObject;
    llvm::Function* newBinaryObject;
    llvm::Function* sendMessage;
    llvm::Function* createBlock;
    llvm::Function* emitBlockReturn;
};

struct TExceptionAPI {
    llvm::Function* gxx_personality;
    llvm::Function* cxa_begin_catch;
    llvm::Function* cxa_end_catch;
    llvm::Function* getBlockReturnType;

    llvm::StructType* blockReturnType;
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

    void initializeFromModule(llvm::Module* module) {
        object      = module->getTypeByName("struct.TObject");
        klass       = module->getTypeByName("struct.TClass");
        context     = module->getTypeByName("struct.TContext");
        block       = module->getTypeByName("struct.TBlock");
        dictionary  = module->getTypeByName("struct.TDictionary");
        method      = module->getTypeByName("struct.TMethod");
        symbol      = module->getTypeByName("struct.TSymbol");
        objectArray = module->getTypeByName("struct.TObjectArray");
        symbolArray = module->getTypeByName("struct.TSymbolArray");
        globals     = module->getTypeByName("struct.TGlobals");
        byteObject  = module->getTypeByName("struct.TByteObject");
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

class MethodCompiler {
private:
    llvm::Module* m_JITModule;
    llvm::Module* m_TypeModule;

    // This structure contains working data which is
    // used during the compilation process.
    struct TJITContext {
        TMethod*            method;       // Smalltalk method we're currently processing
        uint32_t            bytePointer;
        uint32_t            byteCount;
        
        llvm::Function*     function;     // LLVM function that is created based on method
        llvm::Value*        methodPtr; // LLVM representation for Smalltalk's method object
        llvm::Value*        arguments;    // LLVM representation for method arguments array
        llvm::Value*        temporaries;  // LLVM representation for method temporaries array
        llvm::Value*        literals;     // LLVM representation for method literals array
        llvm::Value*        self;         // LLVM representation for current object
        llvm::Value*        selfFields;   // LLVM representation for current object's fields
        
        TInstruction instruction;         // currently processed instruction
        // Builder inserts instructions into basic blocks
        llvm::IRBuilder<>*  builder;
        llvm::Value*        llvmContext;
        llvm::Value*        llvmBlockContext;
        
        llvm::BasicBlock*   landingpadBB;
        bool                methodHasBlockReturn;
        
        // Value stack is used as a FIFO value holder during the compilation process.
        // Software VM uses object arrays to hold the values in dynamic.
        // Instead we're interpriting the push, pop and assign instructions
        // as a commands which values should be linked together. For example,
        // two subsequent instructions 'pushTemporary 1' and 'assignInstance 2'
        // will be linked together with effect of instanceVariables[2] = temporaries[1]

        bool hasValue() { return !valueStack.empty(); }
        void pushValue(llvm::Value* value) { valueStack.push_back(value); }
        llvm::Value* lastValue() { return valueStack.back(); }
        llvm::Value* popValue() {
            if (valueStack.empty()) {
                // Stack underflow due to continiuoslypopping the values, 
                // like in blockReturn stackReturn
                // FIXME Do this in a more clever way
                //return m_globals.nilObject;
                printf("JIT: Value stack underflow!\n");
            }
            
            llvm::Value* value = valueStack.back();
            valueStack.pop_back();
            return value;
        }
        
        TJITContext(TMethod* method) : method(method),
            bytePointer(0), function(0), methodPtr(0), arguments(0),
            temporaries(0), literals(0), self(0), selfFields(0), builder(0), llvmContext(0),
            landingpadBB(0), methodHasBlockReturn(false)
        {
            byteCount = method->byteCodes->getSize();
            valueStack.reserve(method->stackSize);
        };

        ~TJITContext() { if (builder) delete builder; }
    private:    
        std::vector<llvm::Value*> valueStack;
    };

    std::map<uint32_t, llvm::BasicBlock*> m_targetToBlockMap;
    void scanForBranches(TJITContext& jit, uint32_t byteCount = 0);

    std::map<std::string, llvm::Function*> m_blockFunctions;
    
    TObjectTypes ot;
    TJITGlobals    m_globals;
    TRuntimeAPI    m_runtimeAPI;
    TExceptionAPI  m_exceptionAPI;
    
    void writePreamble(TJITContext& jit, bool isBlock = false);
    void writeFunctionBody(TJITContext& jit, uint32_t byteCount = 0);
    void writeLandingpadBB(TJITContext& jit);

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

    void printOpcode(TInstruction instruction);
    
    llvm::Value*    createArray(TJITContext& jit, uint32_t elementsCount);
    llvm::Function* createFunction(TMethod* method);
public:
    llvm::Function* compileMethod(TMethod* method);

    MethodCompiler(
        llvm::Module* JITModule,
        llvm::Module* TypeModule,
        TRuntimeAPI   api,
        TExceptionAPI exceptionApi
    )
        : m_JITModule(JITModule), m_TypeModule(TypeModule),
          m_runtimeAPI(api), m_exceptionAPI(exceptionApi)
    {
        /* we can get rid of m_TypeModule by linking m_JITModule with TypeModule
        std::string linkerErrorMessages;
        bool linkerFailed = llvm::Linker::LinkModules(m_JITModule, TypeModule, llvm::Linker::PreserveSource, &linkerErrorMessages);
        if (linkerFailed) {
            printf("%s\n", linkerErrorMessages.c_str());
            exit(1);
        }
        */
        ot.initializeFromModule(m_TypeModule);
        m_globals.initializeFromModule(m_JITModule);
    }
};

extern "C" {
    TObject*     newOrdinaryObject(TClass* klass, uint32_t slotSize);
    TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize);
    TObject*     sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments);
    TBlock*      createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer);
    void         emitBlockReturn(TObject* value, TContext* targetContext);
    const void*  getBlockReturnType();
}

class JITRuntime {
private:
    llvm::FunctionPassManager* m_functionPassManager;
    
    SmalltalkVM* m_softVM;
    llvm::ExecutionEngine* m_executionEngine;
    MethodCompiler* m_methodCompiler;

    llvm::Module* m_JITModule;
    llvm::Module* m_TypeModule;

    //typedef std::map<std::string, llvm::Function*> TFunctionMap;
    //typedef std::map<std::string, llvm::Function*>::iterator TFunctionMapIterator;
    //TFunctionMap m_compiledFunctions; //TODO useless var?
    
    TRuntimeAPI   m_runtimeAPI;
    TExceptionAPI m_exceptionAPI;
    
    TObjectTypes ot;
    llvm::GlobalVariable* m_jitGlobals;
    
    static JITRuntime* s_instance;

    TObject* sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments);
    TBlock*  createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer);
    
    friend TObject*     newOrdinaryObject(TClass* klass, uint32_t slotSize);
    friend TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize);
    friend TObject*     sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments);
    friend TBlock*      createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer);
    static JITRuntime*  Instance() { return s_instance; }
    
    void initializeGlobals();
    void initializePassManager();
    
    //uses ot types. dont forget to init it before calling this method
    void initializeRuntimeAPI();
    void initializeExceptionAPI();
    
public:
    typedef TObject* (*TMethodFunction)(TContext*);
    
    MethodCompiler* getCompiler() { return m_methodCompiler; }
    SmalltalkVM* getVM() { return m_softVM; }
    llvm::ExecutionEngine* getExecutionEngine() { return m_executionEngine; }
    
    void dumpJIT();
    
    void initialize(SmalltalkVM* softVM);
    ~JITRuntime();
};

struct TBlockReturn {
    TObject*  value;
    TContext* targetContext;
    TBlockReturn(TObject* value, TContext* targetContext)
        : value(value), targetContext(targetContext) { }

    static const void* getBlockReturnType() {
        return reinterpret_cast<const void*>( &typeid(TBlockReturn) ) ;
    }
};

