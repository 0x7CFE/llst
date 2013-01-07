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
        llvm::Value*        methodObject; // LLVM representation for Smalltalk's method object
        llvm::Value*        arguments;    // LLVM representation for method arguments array
        llvm::Value*        temporaries;  // LLVM representation for method temporaries array
        llvm::Value*        literals;     // LLVM representation for method literals array
        llvm::Value*        self;         // LLVM representation for current object
        
        // Value stack is used as a FIFO value holder during the compilation process.
        // Software VM uses object arrays to hold the values in dynamic.
        // Instead we're interpriting the push, pop and assign instructions
        // as a commands which values should be linked together. For example,
        // two subsequent instructions 'pushTemporary 1' and 'assignInstance 2'
        // will be linked together with effect of instanceVariables[2] = temporaries[1]

        void pushValue(llvm::Value* value) { valueStack.push_back(value); }
        llvm::Value* lastValue() { return valueStack.back(); }
        llvm::Value* popValue() {
            llvm::Value* value = valueStack.back();
            valueStack.pop_back();
            return value;
        }
        
        TJITContext(TMethod* method) : method(method), bytePointer(0) {
            byteCount = method->byteCodes->getSize();
            valueStack.reserve(method->stackSize);
        };
        
    private:    
        std::vector<llvm::Value*> valueStack;
    };

    std::map<uint32_t, llvm::BasicBlock*> m_targetToBlockMap;
    void scanForBranches(TJITContext& jitContext);

    struct TObjectTypes {
        llvm::StructType* object;
        llvm::StructType* context;
        llvm::StructType* method;
        llvm::StructType* symbol;
        llvm::StructType* objectArray;
        llvm::StructType* symbolArray;
    };
    TObjectTypes ot;
    void initObjectTypes();

    void writePreamble(llvm::IRBuilder<>& builder, TJITContext& context);
    void doSpecial(uint8_t opcode, llvm::IRBuilder<>& builder, TJITContext& context);
    
    llvm::Function* createFunction(TMethod* method);
    llvm::Function* compileBlock(TJITContext& context);
public:
    llvm::Function* compileMethod(TMethod* method);

    MethodCompiler(llvm::Module* JITModule, llvm::Module* TypeModule)
        : m_JITModule(JITModule), m_TypeModule(TypeModule)
    {
        /* we can get rid of m_TypeModule by linking m_JITModule with TypeModule
        std::string linkerErrorMessages;
        bool linkerFailed = llvm::Linker::LinkModules(m_JITModule, TypeModule, llvm::Linker::PreserveSource, &linkerErrorMessages);
        if (linkerFailed) {
            printf("%s\n", linkerErrorMessages.c_str());
            exit(1);
        }
        */
        initObjectTypes();
    }
};

extern "C" {
    TObject* newOrdinaryObject(TClass* klass, uint32_t slotSize);
    TObject* newBinaryObject(TClass* klass, uint32_t dataSize);
}

class JITRuntime {
private:
    SmalltalkVM* m_softVM;
    llvm::ExecutionEngine* m_executionEngine;
    MethodCompiler* m_methodCompiler;

    llvm::Module* m_JITModule;
    llvm::Module* m_TypeModule;
    
    static JITRuntime* s_instance;

    friend TObject* newOrdinaryObject(TClass* klass, uint32_t slotSize);
    friend TObject* newBinaryObject(TClass* klass, uint32_t dataSize);
    static JITRuntime* Instance() { return s_instance; }
public:
    
    MethodCompiler* getCompiler() { return m_methodCompiler; }
    SmalltalkVM* getVM() { return m_softVM; }
    
    void dumpJIT();
    
    void initialize(SmalltalkVM* softVM);
    ~JITRuntime();
};
