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

#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/ExecutionEngine/GenericValue.h>

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
        Err.print("JITRuntime.cpp", errs());
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
    FunctionType* newBinaryObjectType   = FunctionType::get(byteObjectType, params, false);
    
    // Creating function references
    
    m_RuntimeAPI.newOrdinaryObject = Function::Create(newOrdinaryObjectType, Function::ExternalLinkage, "newOrdinaryObject", m_JITModule);
    m_RuntimeAPI.newBinaryObject   = Function::Create(newBinaryObjectType, Function::ExternalLinkage, "newBinaryObject", m_JITModule);
    m_RuntimeAPI.sendMessage       = Function::Create(newBinaryObjectType, Function::ExternalLinkage, "sendMessage", m_JITModule);
    
    std::string error;
    m_executionEngine = EngineBuilder(m_JITModule).setEngineKind(EngineKind::JIT).setErrorStr(&error).create();
    if(!m_executionEngine) {
        errs() << error;
        exit(1);
    }

    // Mapping the function references to actual functions
    m_executionEngine->addGlobalMapping(m_RuntimeAPI.newOrdinaryObject, reinterpret_cast<void*>(& ::newOrdinaryObject));
    m_executionEngine->addGlobalMapping(m_RuntimeAPI.newBinaryObject, reinterpret_cast<void*>(& ::newBinaryObject));
    m_executionEngine->addGlobalMapping(m_RuntimeAPI.sendMessage, reinterpret_cast<void*>(& ::sendMessage));
    
    ot.initializeFromModule(m_TypeModule);
    
    initializeGlobals();
    
    // Initializing the method compiler
    m_methodCompiler = new MethodCompiler(m_JITModule, m_TypeModule, m_RuntimeAPI);
}

void JITRuntime::dumpJIT()
{
    verifyModule(*m_JITModule);
    m_JITModule->dump();
}

JITRuntime::~JITRuntime() {
    // TODO Finalize stuff and dispose memory
}

TObject* JITRuntime::sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments)
{
    // First of all we need to find the actual method object
    TObject* receiver = arguments->getField(0);
    TClass*  receiverClass = globals.smallIntClass;
    if (! isSmallInteger(receiver))
        receiverClass = receiver->getClass();

    // Searching for the actual method to be called
    hptr<TMethod> method = m_softVM->newPointer(m_softVM->lookupMethod(message, receiverClass));
    // TODO #doesNotUnderstand:

    llvm::Function* function = 0;
    TByteArray* bitCode = method->llvmBitcode;

    // Checking if we already have the compiled llvm code 
    if (bitCode == globals.nilObject) {
        // Compiling the method into LLVM IR
        function = JITRuntime::Instance()->getCompiler()->compileMethod(method);
        // TODO Store the bitCode into the object
    } else {
        // Method is already compiled, parsing the IR and loading function
        // TODO function = parseBitcode(method->llvmBitcode);
    }

    // Preparing the context objects. Because we do not call the software
    // implementation here, we do not need to allocate the stack object
    // because it is not used by JIT runtime. We also may skip the proper
    // initialization of various objects such as stackTop and bytePointer.
    
    // Protecting the pointer
    hptr<TObjectArray> messageArguments = m_softVM->newPointer(arguments);
    hptr<TContext>     newContext = m_softVM->newObject<TContext>();
    hptr<TObjectArray> newTemps   = m_softVM->newObject<TObjectArray>(getIntegerValue(method->temporarySize));
    
    newContext->temporaries       = newTemps;
    newContext->arguments         = messageArguments;
    newContext->method            = method;
    newContext->previousContext   = callingContext;
    
    // Calling the method and returning the result
    std::vector<GenericValue> args;
    args.push_back(GenericValue(newContext));
    GenericValue result = m_executionEngine->runFunction(function, args);
    return (TObject*) GVTOP(result);
}

void JITRuntime::initializeGlobals() {
    GlobalValue* m_jitGlobals = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals", ot.globals) );
    m_executionEngine->addGlobalMapping(m_jitGlobals, reinterpret_cast<void*>(&globals));
    
    GlobalValue* gNil = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.nilObject", ot.object) );
    m_executionEngine->addGlobalMapping(gNil, reinterpret_cast<void*>(&globals.nilObject));
    
    GlobalValue* gTrue = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.trueObject", ot.object) );
    m_executionEngine->addGlobalMapping(gTrue, reinterpret_cast<void*>(&globals.trueObject));
    
    GlobalValue* gFalse = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.false", ot.object) );
    m_executionEngine->addGlobalMapping(gFalse, reinterpret_cast<void*>(&globals.falseObject));
    
    GlobalValue* gSmallIntClass = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.smallIntClass", ot.klass) );
    m_executionEngine->addGlobalMapping(gSmallIntClass, reinterpret_cast<void*>(&globals.smallIntClass));

    GlobalValue* gArrayClass = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.arrayClass", ot.klass) );
    m_executionEngine->addGlobalMapping(gArrayClass, reinterpret_cast<void*>(&globals.arrayClass));
}

extern "C" {
    
TObject* newOrdinaryObject(TClass* klass, uint32_t slotSize) {
    return JITRuntime::Instance()->getVM()->newOrdinaryObject(klass, slotSize);
}

TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize) {
    return JITRuntime::Instance()->getVM()->newBinaryObject(klass, dataSize);
}

TObject* sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments) {
    return JITRuntime::Instance()->sendMessage(callingContext, message, arguments);
}

}

