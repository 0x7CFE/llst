/*
*    JITRuntime.cpp
*
*    LLST Runtime environment
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

#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
#include <llvm/LinkAllPasses.h>

#include <llvm/CodeGen/GCs.h>
#include <llstPass.h>

#include <iostream>
#include <sstream>

using namespace llvm;

JITRuntime* JITRuntime::s_instance = 0;

void JITRuntime::printStat()
{
    float hitRatio = (float) 100 * m_cacheHits / (m_cacheHits + m_cacheMisses);
    float blockHitRatio = (float) 100 * m_blockCacheHits / (m_blockCacheHits + m_blockCacheMisses);
    
    printf(
        "JIT Runtime stat:\n"
        "\tMessages dispatched: %12d\n"
        "\tObjects  allocated:  %12d\n"
        "\tBlocks   invoked:    %12d\n"
        "\tBlock    cache hits: %12d  misses %10d ratio %6.2f %%\n"
        "\tMessage  cache hits: %12d  misses %10d ratio %6.2f %%\n",
            
        m_messagesDispatched,
        m_objectsAllocated,
        m_blocksInvoked,
        m_blockCacheHits, m_blockCacheMisses, blockHitRatio,
        m_cacheHits, m_cacheMisses, hitRatio
    );
    PrintStatistics(outs());
}


void JITRuntime::initialize(SmalltalkVM* softVM)
{
    s_instance = this;
    m_softVM = softVM;
    
    // Initializing LLVM subsystem
    InitializeNativeTarget();
    linkShadowStackGC();
    EnableStatistics();
    
    LLVMContext& llvmContext = getGlobalContext();
    
    // Initializing JIT module.
    // All JIT functions will be created here
    SMDiagnostic Err;
    m_JITModule = ParseIRFile("../include/llvm_types.ll", Err, llvmContext); // FIXME Hardcoded path
    if (!m_JITModule) {
        Err.print("JITRuntime.cpp", errs());
        exit(1);
    }
    
    // Providing the memory management interface to the JIT module
    // FIXME Think about interfacing the MemoryManager directly
    // These are then used as an allocator function return types
    
    TargetOptions Opts;
    Opts.JITExceptionHandling = true;
    Opts.GuaranteedTailCallOpt = true;
//    Opts.JITEmitDebugInfo = true;
//     Opts.PrintMachineCode = true;
    
    std::string error;
    m_executionEngine = EngineBuilder(m_JITModule)
                            .setEngineKind(EngineKind::JIT)
                            .setErrorStr(&error)
                            .setTargetOptions(Opts)
                            .setOptLevel(CodeGenOpt::Aggressive)
                            .create();
    
    if (!m_executionEngine) {
        errs() << error;
        exit(1);
    }
    
    m_baseTypes.initializeFromModule(m_JITModule);
    
    initializeGlobals();
    initializePassManager();
    initializeRuntimeAPI();
    initializeExceptionAPI();
    createExecuteProcessFunction();
    
    // Initializing the method compiler
    m_methodCompiler = new MethodCompiler(m_JITModule, m_runtimeAPI, m_exceptionAPI);
    
    // Initializing caches
    memset(&m_blockFunctionLookupCache, 0, sizeof(m_blockFunctionLookupCache));
    memset(&m_functionLookupCache, 0, sizeof(m_functionLookupCache));
    m_blockCacheHits = 0;
    m_blockCacheMisses = 0;
    m_cacheHits = 0;
    m_cacheMisses = 0;
    m_messagesDispatched = 0;
    m_blocksInvoked = 0;
    m_objectsAllocated = 0;
}

JITRuntime::~JITRuntime() {
    // Finalize stuff and dispose memory
    if (m_functionPassManager)
        delete m_functionPassManager;
    if (m_modulePassManager)
        delete m_modulePassManager;
}

TBlock* JITRuntime::createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer)
{
    // Protecting pointer
    hptr<TContext> previousContext = m_softVM->newPointer(callingContext);
    
    // Creating new context object and inheriting context variables
    // NOTE We do not allocating stack because it's not used in LLVM
    hptr<TBlock> newBlock      = m_softVM->newObject<TBlock>();
    newBlock->argumentLocation = newInteger(argLocation);
    newBlock->blockBytePointer = newInteger(bytePointer);
    newBlock->method           = previousContext->method;
    newBlock->arguments        = previousContext->arguments;
    newBlock->temporaries      = previousContext->temporaries;
    
    newBlock->bytePointer = 0;
    newBlock->stackTop = 0;
    
    // Assigning creatingContext depending on the hierarchy
    // Nested blocks inherit the outer creating context
    if (previousContext->getClass() == globals.blockClass)
        newBlock->creatingContext = previousContext.cast<TBlock>()->creatingContext;
    else
        newBlock->creatingContext = previousContext;
    
    return newBlock;
}

JITRuntime::TMethodFunction JITRuntime::lookupFunctionInCache(TMethod* method)
{
    uint32_t hash = reinterpret_cast<uint32_t>(method) ^ reinterpret_cast<uint32_t>(method->name); // ^ 0xDEADBEEF;
    TFunctionCacheEntry& entry = m_functionLookupCache[hash % LOOKUP_CACHE_SIZE];
    
    if (entry.method == method) {
        m_cacheHits++;
        return entry.function;
    } else {
        m_cacheMisses++;
        return 0;
    }
}

JITRuntime::TBlockFunction JITRuntime::lookupBlockFunctionInCache(TMethod* containerMethod, uint32_t blockOffset)
{
    uint32_t hash = reinterpret_cast<uint32_t>(containerMethod) ^ blockOffset;
    TBlockFunctionCacheEntry& entry = m_blockFunctionLookupCache[hash % LOOKUP_CACHE_SIZE];
    
    if (entry.containerMethod == containerMethod && entry.blockOffset == blockOffset) {
        m_blockCacheHits++;
        return entry.function;
    } else {
        m_blockCacheMisses++;
        return 0;
    }
}

void JITRuntime::updateFunctionCache(TMethod* method, TMethodFunction function)
{
    uint32_t hash = reinterpret_cast<uint32_t>(method) ^ reinterpret_cast<uint32_t>(method->name); // ^ 0xDEADBEEF;
    TFunctionCacheEntry& entry = m_functionLookupCache[hash % LOOKUP_CACHE_SIZE];
    
    entry.method   = method;
    entry.function = function;
}

void JITRuntime::updateBlockFunctionCache(TMethod* containerMethod, uint32_t blockOffset, TBlockFunction function)
{
    uint32_t hash = reinterpret_cast<uint32_t>(containerMethod) ^ blockOffset;
    TBlockFunctionCacheEntry& entry = m_blockFunctionLookupCache[hash % LOOKUP_CACHE_SIZE];
    
    entry.containerMethod = containerMethod;
    entry.blockOffset = blockOffset;
    entry.function = function;
}


void JITRuntime::optimizeFunction(Function* function)
{
    m_modulePassManager->run(*m_JITModule); 
    
    // Running the optimization passes on a function
    m_functionPassManager->run(*function);
}

TObject* JITRuntime::invokeBlock(TBlock* block, TContext* callingContext)
{
    // Guessing the block function name
    const uint16_t blockOffset = getIntegerValue(block->blockBytePointer);
    
    TBlockFunction compiledBlockFunction = lookupBlockFunctionInCache(block->method, blockOffset);
    
    if (! compiledBlockFunction) {
        std::ostringstream ss;
        ss << block->method->klass->name->toString() << ">>" << block->method->name->toString() << "@" << blockOffset;
        std::string blockFunctionName = ss.str();
        
        Function* blockFunction = m_JITModule->getFunction(blockFunctionName);
        if (!blockFunction) {
            // Block functions are created when wrapping method gets compiled.
            // If function was not found then the whole method needs compilation.
            
            // Compiling function and storing it to the table for further use
            outs() << "Compiling block method " << blockFunctionName << "\n";
            Function* methodFunction = m_methodCompiler->compileMethod(block->method, callingContext);
            blockFunction = m_JITModule->getFunction(blockFunctionName);
            if (!methodFunction || !blockFunction) {
                // Something is really wrong!
                outs() << "JIT: Fatal error in invokeBlock for " << blockFunctionName << "\n";
                exit(1);
            }
            
//             if (verifyModule(*m_JITModule)) {
//                 outs() << "Module verification failed.\n";
//                 //exit(1);
//             }
            
            optimizeFunction(blockFunction);
        }
        
        compiledBlockFunction = reinterpret_cast<TBlockFunction>(m_executionEngine->getPointerToFunction(blockFunction));
        updateBlockFunctionCache(block->method, blockOffset, compiledBlockFunction);
//         outs() << *blockFunction;
    }
    
    block->previousContext = callingContext->previousContext;
    TObject* result = compiledBlockFunction(block);
    
    return result;
}

TObject* JITRuntime::sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass)
{
    hptr<TObjectArray> messageArguments = m_softVM->newPointer(arguments);
    TMethodFunction compiledMethodFunction = 0;
    TContext*       newContext = 0;
    
    {
        
        // First of all we need to find the actual method object
        TClass*  klass = 0;
        
        if (receiverClass) {
            klass = receiverClass;
        } else {
            TObject* receiver = messageArguments[0];
            klass = isSmallInteger(receiver) ? globals.smallIntClass : receiver->getClass();
        }
        
        // Searching for the actual method to be called
        hptr<TMethod> method = m_softVM->newPointer(m_softVM->lookupMethod(message, klass));
        
        // Checking whether we found a method
        if (method == 0) {
            // Oops. Method was not found. In this case we should
            // send #doesNotUnderstand: message to the receiver
            
            // Looking up the #doesNotUnderstand: method:
            method = m_softVM->newPointer(m_softVM->lookupMethod(globals.badMethodSymbol, klass));
            if (method == 0) {
                // Something goes really wrong.
                // We could not continue the execution
                errs() << "\nCould not locate #doesNotUnderstand:\n";
                exit(1);
            }
            
            // Protecting the selector pointer because it may be invalidated later
            hptr<TSymbol> failedSelector = m_softVM->newPointer(message);
            
            // We're replacing the original call arguments with custom one
            hptr<TObjectArray> errorArguments = m_softVM->newObject<TObjectArray>(2);
            
            // Filling in the failed call context information
            errorArguments[0] = messageArguments[0]; // receiver object
            errorArguments[1] = failedSelector;      // message selector that failed
            
            // Replacing the arguments with newly created one
            messageArguments = errorArguments;
            
            // Continuing the execution just as if #doesNotUnderstand:
            // was the actual selector that we wanted to call
        }
        
        // Searching for the jit compiled function
        compiledMethodFunction = lookupFunctionInCache(method); 
        
        if (! compiledMethodFunction) {
            // If function was not found in the cache looking it in the LLVM directly
            std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
            Function* methodFunction = m_JITModule->getFunction(functionName);
            
            if (! methodFunction) {
                // Compiling function and storing it to the table for further use
                outs() << "Compiling method " << functionName << "\n";
                methodFunction = m_methodCompiler->compileMethod(method, callingContext);
                
    //             outs() << *methodFunction;
                
                if (verifyModule(*m_JITModule)) {
                    outs() << "Module verification failed.\n";
                    exit(1);
                }
                
                
                // Running the optimization passes on a function
                //m_modulePassManager->run(*m_JITModule);   //TODO too expensive to run on each function compilation?
                                                        //we may get rid of TObject::getFields on our own.
                //m_functionPassManager->run(*methodFunction);
                optimizeFunction(methodFunction);
            }
            
            // Calling the method and returning the result
            compiledMethodFunction = reinterpret_cast<TMethodFunction>(m_executionEngine->getPointerToFunction(methodFunction));
    //         outs() << *methodFunction;
            
            updateFunctionCache(method, compiledMethodFunction);
        }
        
        // Preparing the context objects. Because we do not call the software
        // implementation here, we do not need to allocate the stack object
        // because it is not used by JIT runtime. We also may skip the proper
        // initialization of various objects such as stackTop and bytePointer.
        
        // Protecting the pointers before allocation
        hptr<TContext>     previousContext  = m_softVM->newPointer(callingContext);
        
        // Creating context object and temporaries
        hptr<TObjectArray> newTemps   = m_softVM->newObject<TObjectArray>(getIntegerValue(method->temporarySize));
        newContext = m_softVM->newObject<TContext>();
        
        // Initializing context variables
        newContext->temporaries       = newTemps;
        newContext->arguments         = messageArguments;
        newContext->method            = method;
        newContext->previousContext   = previousContext;

        newContext->bytePointer       = 0;
        newContext->stackTop          = 0;
    }
    
//     static int messageDepth = 0;
//     static int messageDepthMax = 0;
//         
//     messageDepth++;
//     messageDepthMax = (messageDepth > messageDepthMax ? messageDepth : messageDepthMax);
    
//     outs() << messageDepth << " on enter, max " << messageDepthMax << "\n";
    TObject* result = compiledMethodFunction(newContext);
//     messageDepth--;
//     outs() << messageDepth << " on exit, max " << messageDepthMax << "\n";
    
    return result;
}

void JITRuntime::initializeGlobals() {
    GlobalValue* m_jitGlobals = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals", m_baseTypes.globals) );
    m_executionEngine->addGlobalMapping(m_jitGlobals, reinterpret_cast<void*>(&globals));
    
    GlobalValue* gNil = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.nilObject", m_baseTypes.object) );
    m_executionEngine->addGlobalMapping(gNil, reinterpret_cast<void*>(globals.nilObject));
    
    GlobalValue* gTrue = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.trueObject", m_baseTypes.object) );
    m_executionEngine->addGlobalMapping(gTrue, reinterpret_cast<void*>(globals.trueObject));
    
    GlobalValue* gFalse = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.falseObject", m_baseTypes.object) );
    m_executionEngine->addGlobalMapping(gFalse, reinterpret_cast<void*>(globals.falseObject));
    
    GlobalValue* gSmallIntClass = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.smallIntClass", m_baseTypes.klass) );
    m_executionEngine->addGlobalMapping(gSmallIntClass, reinterpret_cast<void*>(globals.smallIntClass));
    
    GlobalValue* gArrayClass = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.arrayClass", m_baseTypes.klass) );
    m_executionEngine->addGlobalMapping(gArrayClass, reinterpret_cast<void*>(globals.arrayClass));
    
    GlobalValue* gmessageL = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.<", m_baseTypes.symbol) );
    m_executionEngine->addGlobalMapping(gmessageL, reinterpret_cast<void*>(globals.binaryMessages[0]));
    
    GlobalValue* gmessageLE = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.<=", m_baseTypes.symbol) );
    m_executionEngine->addGlobalMapping(gmessageLE, reinterpret_cast<void*>(globals.binaryMessages[1]));
    
    GlobalValue* gmessagePlus = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals.+", m_baseTypes.symbol) );
    m_executionEngine->addGlobalMapping(gmessagePlus, reinterpret_cast<void*>(globals.binaryMessages[2]));
}

void JITRuntime::initializePassManager() {
    m_functionPassManager = new FunctionPassManager(m_JITModule);
    m_modulePassManager   = new PassManager();
    // Set up the optimizer pipeline.
    // Start with registering info about how the
    // target lays out data structures.
    m_functionPassManager->add(new TargetData(*m_executionEngine->getTargetData()));
    
    // Basic AliasAnslysis support for GVN.
    m_functionPassManager->add(createBasicAliasAnalysisPass());
    
    // Promote allocas to registers.
    m_functionPassManager->add(createPromoteMemoryToRegisterPass());

    // Do simple "peephole" optimizations and bit-twiddling optzns.
    m_functionPassManager->add(createInstructionCombiningPass());

    // Reassociate expressions.
    m_functionPassManager->add(createReassociatePass());

    // Eliminate Common SubExpressions.
    m_functionPassManager->add(createGVNPass());

    m_functionPassManager->add(createAggressiveDCEPass());
    
    m_functionPassManager->add(createTailCallEliminationPass());
    
    // Simplify the control flow graph (deleting unreachable
    // blocks, etc).
    m_functionPassManager->add(createCFGSimplificationPass());
    
    m_functionPassManager->add(createDeadCodeEliminationPass());
    m_functionPassManager->add(createDeadStoreEliminationPass());
    
//     m_functionPassManager->add(createLLSTPass());
    //If llstPass removed GC roots, we may try DCE again
    
//     m_functionPassManager->add(createDeadCodeEliminationPass());
//     m_functionPassManager->add(createDeadStoreEliminationPass());
    
    m_modulePassManager->add(createFunctionInliningPass());
    m_functionPassManager->doInitialization();
}

void JITRuntime::initializeRuntimeAPI() {
    LLVMContext& llvmContext = getGlobalContext();
    
    PointerType* objectType     = m_baseTypes.object->getPointerTo();
    PointerType* classType      = m_baseTypes.klass->getPointerTo();
    PointerType* byteObjectType = m_baseTypes.byteObject->getPointerTo();
    PointerType* contextType    = m_baseTypes.context->getPointerTo();
    PointerType* blockType      = m_baseTypes.block->getPointerTo();
    PointerType* objectSlotType = m_baseTypes.object->getPointerTo()->getPointerTo(); // TObject**
    
    Type* params[] = {
        classType,                    // klass
        Type::getInt32Ty(llvmContext) // size
    };
    FunctionType* newOrdinaryObjectType = FunctionType::get(objectType,     params, false);
    FunctionType* newBinaryObjectType   = FunctionType::get(byteObjectType, params, false);
    
    
    Type* sendParams[] = {
        contextType,                    // callingContext
        m_baseTypes.symbol->getPointerTo(),      // message selector
        m_baseTypes.objectArray->getPointerTo(), // arguments
        classType                       // receiverClass
    };
    FunctionType* sendMessageType  = FunctionType::get(objectType, sendParams, false);
    
    Type* createBlockParams[] = {
        contextType,                  // callingContext
        Type::getInt8Ty(llvmContext), // argLocation
        Type::getInt16Ty(llvmContext) // bytePointer
    };
    FunctionType* createBlockType = FunctionType::get(blockType, createBlockParams, false);
    
    Type* invokeBlockParams[] = {
        blockType,  // block
        contextType // callingContext
    };
    FunctionType* invokeBlockType = FunctionType::get(objectType, invokeBlockParams, false);
    
    
    Type* emitBlockReturnParams[] = {
        objectType, // value
        contextType // targetContext
    };
    FunctionType* emitBlockReturnType = FunctionType::get(Type::getVoidTy(llvmContext), emitBlockReturnParams, false);
    
    Type* checkRootParams[] = {
        objectType,     // value
        objectSlotType  // slot
    };
    FunctionType* checkRootType = FunctionType::get(Type::getVoidTy(llvmContext), checkRootParams, false);
    
    Type* bulkReplaceParams[] = {
        objectType, // destination
        objectType, // sourceStartOffset
        objectType, // source
        objectType, // destinationStopOffset
        objectType  // destinationStartOffset
    };
    FunctionType* bulkReplaceType = FunctionType::get(Type::getInt1Ty(llvmContext), bulkReplaceParams, false);
    
    // Creating function references
    m_runtimeAPI.newOrdinaryObject  = Function::Create(newOrdinaryObjectType, Function::ExternalLinkage, "newOrdinaryObject", m_JITModule);
    m_runtimeAPI.newBinaryObject    = Function::Create(newBinaryObjectType, Function::ExternalLinkage, "newBinaryObject", m_JITModule);
    m_runtimeAPI.sendMessage        = Function::Create(sendMessageType, Function::ExternalLinkage, "sendMessage", m_JITModule);
    m_runtimeAPI.createBlock        = Function::Create(createBlockType, Function::ExternalLinkage, "createBlock", m_JITModule);
    m_runtimeAPI.invokeBlock        = Function::Create(invokeBlockType, Function::ExternalLinkage, "invokeBlock", m_JITModule);
    m_runtimeAPI.emitBlockReturn    = Function::Create(emitBlockReturnType, Function::ExternalLinkage, "emitBlockReturn", m_JITModule);
    m_runtimeAPI.checkRoot          = Function::Create(checkRootType, Function::ExternalLinkage, "checkRoot", m_JITModule );
    m_runtimeAPI.bulkReplace        = Function::Create(bulkReplaceType, Function::ExternalLinkage, "bulkReplace", m_JITModule);
    
    // Mapping the function references to actual functions
    m_executionEngine->addGlobalMapping(m_runtimeAPI.newOrdinaryObject, reinterpret_cast<void*>(& ::newOrdinaryObject));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.newBinaryObject, reinterpret_cast<void*>(& ::newBinaryObject));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.sendMessage, reinterpret_cast<void*>(& ::sendMessage));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.createBlock, reinterpret_cast<void*>(& ::createBlock));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.invokeBlock, reinterpret_cast<void*>(& ::invokeBlock));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.emitBlockReturn, reinterpret_cast<void*>(& ::emitBlockReturn));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.checkRoot, reinterpret_cast<void*>(& ::checkRoot));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.bulkReplace, reinterpret_cast<void*>(& ::bulkReplace));
    
    //Type*  rootChainType = m_JITModule->getTypeByName("gc_stackentry")->getPointerTo();
    //GlobalValue* gRootChain    = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("llvm_gc_root_chain", rootChainType) );
    GlobalValue* gRootChain = m_JITModule->getGlobalVariable("llvm_gc_root_chain");
    m_executionEngine->addGlobalMapping(gRootChain, reinterpret_cast<void*>(&llvm_gc_root_chain));
}

void JITRuntime::initializeExceptionAPI() {
    LLVMContext& Context = getGlobalContext();
    Type* Int32Ty        = Type::getInt32Ty(Context);
    Type* Int8PtrTy      = Type::getInt8PtrTy(Context);
    Type* VoidTy         = Type::getVoidTy(Context);
    Type* throwParams[] = { Int8PtrTy, Int8PtrTy, Int8PtrTy };
    
    m_exceptionAPI.gcc_personality = Function::Create(FunctionType::get(Int32Ty, true), Function::ExternalLinkage, "__gcc_personality_v0", m_JITModule);
    m_exceptionAPI.cxa_begin_catch = Function::Create(FunctionType::get(Int8PtrTy, Int8PtrTy, false), Function::ExternalLinkage, "__cxa_begin_catch", m_JITModule);
    m_exceptionAPI.cxa_end_catch   = Function::Create(FunctionType::get(VoidTy, false), Function::ExternalLinkage, "__cxa_end_catch", m_JITModule);
    m_exceptionAPI.cxa_allocate_exception = Function::Create(FunctionType::get(Int8PtrTy, Int32Ty, false), Function::ExternalLinkage, "__cxa_allocate_exception", m_JITModule);
    m_exceptionAPI.cxa_throw       = Function::Create(FunctionType::get(VoidTy, throwParams, false), Function::ExternalLinkage, "__cxa_throw", m_JITModule);
    
    m_exceptionAPI.blockReturnType = cast<GlobalValue>(m_JITModule->getOrInsertGlobal("blockReturnType", Int8PtrTy));
    m_executionEngine->addGlobalMapping(m_exceptionAPI.blockReturnType, TBlockReturn::getBlockReturnType());
    
    m_exceptionAPI.contextTypeInfo = cast<GlobalValue>(m_JITModule->getOrInsertGlobal("contextTypeInfo", Int8PtrTy));
    m_executionEngine->addGlobalMapping(m_exceptionAPI.contextTypeInfo, const_cast<void*>(reinterpret_cast<const void*>( &typeid(TContext*) )));
}

void JITRuntime::createExecuteProcessFunction() {
    Type* executeProcessParams[] = {
        m_baseTypes.process->getPointerTo()
    };
    FunctionType* executeProcessType = FunctionType::get(Type::getInt32Ty(m_JITModule->getContext()), executeProcessParams, false);
    
    Function* executeProcess = cast<Function>( m_JITModule->getOrInsertFunction("executeProcess", executeProcessType));
    executeProcess->setGC("shadow-stack");
    BasicBlock* entry = BasicBlock::Create(m_JITModule->getContext(), "", executeProcess);
    
    IRBuilder<> builder(entry);
    
    Value* process = (Value*) (executeProcess->arg_begin());
    process->setName("process");
    
    Value* processHolder = builder.CreateAlloca(m_baseTypes.process->getPointerTo());
    Function* gcRootFunction = m_JITModule->getFunction("llvm.gcroot");
    builder.CreateCall2(gcRootFunction, builder.CreateBitCast(processHolder, builder.getInt8PtrTy()->getPointerTo()), ConstantPointerNull::get(builder.getInt8PtrTy()) );
    builder.CreateStore(process, processHolder);
    
    Value* contextPtr  = builder.CreateStructGEP(process, 1);
    Value* context     = builder.CreateLoad(contextPtr);
    Value* argsPtr     = builder.CreateStructGEP(context, 2);
    Value* args        = builder.CreateLoad(argsPtr);
    Value* methodPtr   = builder.CreateStructGEP(context, 1);
    Value* method      = builder.CreateLoad(methodPtr);
    Value* selectorPtr = builder.CreateStructGEP(method, 1);
    Value* selector    = builder.CreateLoad(selectorPtr);
    
    BasicBlock* OK   = BasicBlock::Create(m_JITModule->getContext(), "OK", executeProcess);
    BasicBlock* Fail = BasicBlock::Create(m_JITModule->getContext(), "FAIL", executeProcess);
    
    Value* sendMessageArgs[] = {
        context,
        selector,
        args,
        ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()) 
    };
    
    builder.CreateInvoke(m_runtimeAPI.sendMessage, OK, Fail, sendMessageArgs);
    
    builder.SetInsertPoint(OK);
    builder.CreateRet( builder.getInt32(SmalltalkVM::returnReturned) );
    
    builder.SetInsertPoint(Fail);
    Type* caughtType = StructType::get(builder.getInt8PtrTy(), builder.getInt32Ty(), NULL);
    
    LandingPadInst* exceptionStruct = builder.CreateLandingPad(caughtType, m_exceptionAPI.gcc_personality, 1);
    exceptionStruct->addClause(m_exceptionAPI.contextTypeInfo);
    
    Value* exceptionObject  = builder.CreateExtractValue(exceptionStruct, 0);
    Value* thrownException  = builder.CreateCall(m_exceptionAPI.cxa_begin_catch, exceptionObject);
    Value* thrownContext    = builder.CreateBitCast(thrownException, m_baseTypes.context->getPointerTo());
    
    process = builder.CreateLoad(processHolder);
    contextPtr = builder.CreateStructGEP(process, 1);
    builder.CreateStore(thrownContext, contextPtr);
    
    builder.CreateCall(m_exceptionAPI.cxa_end_catch);
    builder.CreateRet( builder.getInt32(SmalltalkVM::returnError) );
}

extern "C" {

TObject* newOrdinaryObject(TClass* klass, uint32_t slotSize)
{
//     printf("newOrdinaryObject(%p '%s', %d)\n", klass, klass->name->toString().c_str(), slotSize);
    JITRuntime::Instance()->m_objectsAllocated++;
    return JITRuntime::Instance()->getVM()->newOrdinaryObject(klass, slotSize);
}

TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize)
{
//     printf("newBinaryObject(%p '%s', %d)\n", klass, klass->name->toString().c_str(), dataSize);
    JITRuntime::Instance()->m_objectsAllocated++;
    return JITRuntime::Instance()->getVM()->newBinaryObject(klass, dataSize);
}

TObject* sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass)
{
//     printf("sendMessage(%p, #%s, %p)\n",
//            callingContext,
//            message->toString().c_str(),
//            arguments);
    
//     TObject* self = arguments->getField(0);
//     printf("\tself = %p\n", self);
//     
//     TClass* klass = isSmallInteger(self) ? globals.smallIntClass : self->getClass();
//     printf("\tself class = %p\n", klass);
//     printf("\tself class name = '%s'\n", klass->name->toString().c_str());
    JITRuntime::Instance()->m_messagesDispatched++;
    return JITRuntime::Instance()->sendMessage(callingContext, message, arguments, receiverClass);
}

TBlock* createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer)
{
//     printf("createBlock(%p, %d, %d)\n",
//         callingContext,
//         (uint32_t) argLocation,
//         (uint32_t) bytePointer );
    
    return JITRuntime::Instance()->createBlock(callingContext, argLocation, bytePointer);
}

TObject* invokeBlock(TBlock* block, TContext* callingContext)
{
//     printf("invokeBlock %p, %p\n", block, callingContext);
    JITRuntime::Instance()->m_blocksInvoked++;
    return JITRuntime::Instance()->invokeBlock(block, callingContext);
}

void emitBlockReturn(TObject* value, TContext* targetContext)
{
    printf("emitBlockReturn(%p, %p)\n", value, targetContext);
    throw TBlockReturn(value, targetContext);
}

void checkRoot(TObject* value, TObject** objectSlot)
{
//     printf("checkRoot %p, %p\n", value, objectSlot);
    JITRuntime::Instance()->getVM()->checkRoot(value, objectSlot);
}

bool bulkReplace(TObject* destination,
                TObject* destinationStartOffset,
                TObject* destinationStopOffset,
                TObject* source,
                TObject* sourceStartOffset)
{
    return JITRuntime::Instance()->getVM()->doBulkReplace(destination,
                                                        destinationStartOffset,
                                                        destinationStopOffset,
                                                        source,
                                                        sourceStartOffset);
}

}
