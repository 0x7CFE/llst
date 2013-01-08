/*
 *    main.cpp
 *
 *    Program entry point
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

#include <jit.h>

#include "llvm/Support/TargetSelect.h"
#include <llvm/Support/IRReader.h>
#include <llvm/Analysis/Verifier.h>

using namespace llvm;

JITRuntime* JITRuntime::s_instance = 0;

void JITRuntime::initialize(SmalltalkVM* softVM)
{
    m_softVM = softVM;
    
    // Initializing LLVM subsystem
    InitializeNativeTarget();
    
    LLVMContext& llvmContext = getGlobalContext();

    // Initializing types module
    SMDiagnostic Err;
    m_TypeModule = ParseIRFile("../include/llvm_types.ll", Err, llvmContext); // FIXME Hardcoded path
    if (!m_TypeModule) {
        Err.Print("JITRuntime.cpp", errs());
        exit(1);
    }
    
    // Initializing JIT module.
    // All JIT functions will be created here
    m_JITModule = new Module("jit", llvmContext);

    // Providing the memory management interface to the JIT module
    // FIXME Think about interfacing the MemoryManager directly

    // These are then used as an allocator function return types
    StructType* objectType     = m_TypeModule->getTypeByName("struct.TObject");
    StructType* byteObjectType = m_TypeModule->getTypeByName("struct.TByteObject");

    Type* params[] = {
        objectType->getPointerTo(),   // klass
        Type::getInt32Ty(llvmContext) // size
    };
    FunctionType* newOrdinaryObjectType = FunctionType::get(objectType, params, false);
    FunctionType* newBnaryObjectType    = FunctionType::get(byteObjectType, params, false);
    
    // Creating function references
    m_newOrdinaryObjectFunction = cast<Function>(m_JITModule->getOrInsertFunction("newOrdinaryObject", newOrdinaryObjectType));
    m_newBinaryObjectFunction   = cast<Function>(m_JITModule->getOrInsertFunction("newBinaryObject", newBnaryObjectType));

    // Marking functions as external
    m_newOrdinaryObjectFunction->setLinkage(Function::ExternalLinkage);
    m_newBinaryObjectFunction->setLinkage(Function::ExternalLinkage);
    
    // Initializing the method compiler
    m_methodCompiler = new MethodCompiler(m_JITModule, m_TypeModule);
    
    std::string error;
    m_executionEngine = EngineBuilder(m_JITModule).setEngineKind(EngineKind::JIT).setErrorStr(&error).create();
    if(!m_executionEngine) {
        errs() << error;
        exit(1);
    }

    // Mapping the function references to actual functions
    m_executionEngine->addGlobalMapping(m_newOrdinaryObjectFunction, reinterpret_cast<void*>(&newOrdinaryObject));
    m_executionEngine->addGlobalMapping(m_newBinaryObjectFunction, reinterpret_cast<void*>(&newBinaryObject));

    ot.initializeFromModule(m_TypeModule);
    
    // Mapping the globals into the JIT module
    GlobalValue* m_jitGlobals = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals", ot.globals) );
    m_executionEngine->addGlobalMapping(m_jitGlobals, reinterpret_cast<void*>(&globals));
}

void JITRuntime::dumpJIT()
{
    verifyModule(*m_JITModule);
    m_JITModule->dump();
}

JITRuntime::~JITRuntime() {
    // TODO Finalize stuff and dispose memory
}

extern "C" {
    
TObject* newOrdinaryObject(TClass* klass, uint32_t slotSize) {
    return JITRuntime::Instance()->getVM()->newOrdinaryObject(klass, slotSize);
}

TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize) {
    return JITRuntime::Instance()->getVM()->newBinaryObject(klass, dataSize);
}
    
}

