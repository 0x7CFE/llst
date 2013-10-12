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
#include <primitives.h>

#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/InstIterator.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/ExecutionEngine/GenericValue.h>

#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
#include <llvm/LinkAllPasses.h>

#include <llvm/CodeGen/GCs.h>
#include <llstPass.h>
#include <llstDebuggingPass.h>

#include <iostream>
#include <sstream>
#include <cstring>

using namespace llvm;

JITRuntime* JITRuntime::s_instance = 0;

static bool compareByHitCount(const JITRuntime::THotMethod* m1, const JITRuntime::THotMethod* m2) 
{
    return m1->hitCount < m2->hitCount;
}

void JITRuntime::printStat()
{
    float hitRatio = (float) 100 * m_cacheHits / (m_cacheHits + m_cacheMisses);
    float blockHitRatio = (float) 100 * m_blockCacheHits / (m_blockCacheHits + m_blockCacheMisses);
    
    std::printf(
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
    
    std::vector<THotMethod*> hotMethods;
    
    for (THotMethodsMap::iterator iMethod = m_hotMethods.begin(); iMethod != m_hotMethods.end(); ++iMethod)
        hotMethods.push_back(& iMethod->second);
    
    std::sort(hotMethods.begin(), hotMethods.end(), compareByHitCount);
    
    std::printf("\nHot methods:\n");
    std::printf("\tHit count\tMethod name\n");
    for (int i = 0; i < 50; i++) {
        if (hotMethods.empty())
            break;
        
        THotMethod* hotMethod = hotMethods.back(); hotMethods.pop_back();
        if (!hotMethod->methodFunction)
            continue;
        
        std::printf("\t%d\t\t%s (%d sites)\n", hotMethod->hitCount, hotMethod->methodFunction->getName().str().c_str(), hotMethod->callSites.size());
        
        std::map<uint32_t, TCallSite>::iterator iSite = hotMethod->callSites.begin();
        for (; iSite != hotMethod->callSites.end(); ++iSite) {
            std::printf("\t\t%-20s (index %d, offset %d) class hits: ", 
                   iSite->second.messageSelector->toString().c_str(), 
                   iSite->first, 
                   m_methodCompiler->getCallSiteOffset(iSite->first));
            
            std::map<TClass*, uint32_t>::iterator iClassHit = iSite->second.classHits.begin();
            for (; iClassHit != iSite->second.classHits.end(); ++iClassHit)
                std::printf("(%s %d) ", iClassHit->first->name->toString().c_str(), iClassHit->second);
            
            std::printf("\n");
        }
        
    }
    std::printf("\n");
    
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
        std::exit(1);
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
        std::exit(1);
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
    std::memset(&m_blockFunctionLookupCache, 0, sizeof(m_blockFunctionLookupCache));
    std::memset(&m_functionLookupCache, 0, sizeof(m_functionLookupCache));
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
    m_executionEngine->removeModule(m_JITModule);
    delete m_JITModule;
    delete m_executionEngine;
    delete m_functionPassManager;
    delete m_modulePassManager;
    delete m_methodCompiler;
}

TBlock* JITRuntime::createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer)
{
    hptr<TContext> previousContext = m_softVM->newPointer(callingContext);
    
    // Creating new context object and inheriting context variables
    // NOTE We do not allocating stack because it's not used in LLVM
    hptr<TBlock> newBlock      = m_softVM->newObject<TBlock>();
    newBlock->argumentLocation = newInteger(argLocation);
    newBlock->blockBytePointer = newInteger(bytePointer);
    newBlock->method           = previousContext->method;
    newBlock->arguments        = previousContext->arguments;
    newBlock->temporaries      = previousContext->temporaries;
    
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
            Function* methodFunction = m_methodCompiler->compileMethod(block->method);
            blockFunction = m_JITModule->getFunction(blockFunctionName);
            if (!methodFunction || !blockFunction) {
                // Something is really wrong!
                outs() << "JIT: Fatal error in invokeBlock for " << blockFunctionName << "\n";
                std::exit(1);
            }
            
            verifyModule(*m_JITModule, AbortProcessAction);
            
            optimizeFunction(blockFunction);
        }
        
        compiledBlockFunction = reinterpret_cast<TBlockFunction>(m_executionEngine->getPointerToFunction(blockFunction));
        updateBlockFunctionCache(block->method, blockOffset, compiledBlockFunction);
    }
    
    block->previousContext = callingContext->previousContext;
    TObject* result = compiledBlockFunction(block);
    
    return result;
}

TObject* JITRuntime::sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass, uint32_t callSiteIndex)
{
    hptr<TObjectArray> messageArguments = m_softVM->newPointer(arguments);
    TMethodFunction compiledMethodFunction = 0;
    TContext*       newContext = 0;
    hptr<TContext>  previousContext  = m_softVM->newPointer(callingContext);
    
    {
        // First of all we need to find the actual method object
        if (!receiverClass) {
            TObject* receiver = messageArguments[0];
            receiverClass = isSmallInteger(receiver) ? globals.smallIntClass : receiver->getClass();
        }
        
        // Searching for the actual method to be called
        hptr<TMethod> method = m_softVM->newPointer(m_softVM->lookupMethod(message, receiverClass));
        
        // Checking whether we found a method
        if (method == 0) {
            // Oops. Method was not found. In this case we should send #doesNotUnderstand: message to the receiver
            m_softVM->setupVarsForDoesNotUnderstand(method, messageArguments, message, receiverClass);
            // Continuing the execution just as if #doesNotUnderstand: was the actual selector that we wanted to call
        }
        
        // Searching for the jit compiled function
        compiledMethodFunction = lookupFunctionInCache(method); 
        
        if (! compiledMethodFunction) {
            // If function was not found in the cache looking it in the LLVM directly
            std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
            Function* methodFunction = m_JITModule->getFunction(functionName);
            
            if (! methodFunction) {
                // Compiling function and storing it to the table for further use
                methodFunction = m_methodCompiler->compileMethod(method);
                
                verifyModule(*m_JITModule, AbortProcessAction);
                
                optimizeFunction(methodFunction);
            }
            
            // Calling the method and returning the result
            compiledMethodFunction = reinterpret_cast<TMethodFunction>(m_executionEngine->getPointerToFunction(methodFunction));
            updateFunctionCache(method, compiledMethodFunction);
            
            THotMethod& newMethod = m_hotMethods[compiledMethodFunction];
            newMethod.method = method;
            newMethod.methodFunction = methodFunction;
        }
        
        // Updating call site statistics and scheduling method processing
        updateHotSites(compiledMethodFunction, callingContext, message, receiverClass, callSiteIndex);
        
        // Preparing the context objects. Because we do not call the software
        // implementation here, we do not need to allocate the stack object
        // because it is not used by JIT runtime. We also may skip the proper
        // initialization of various objects such as stackTop and bytePointer.
        
        // Creating context object and temporaries
        hptr<TObjectArray> newTemps   = m_softVM->newObject<TObjectArray>(getIntegerValue(method->temporarySize));
        newContext = m_softVM->newObject<TContext>();
        
        // Initializing context variables
        newContext->temporaries       = newTemps;
        newContext->arguments         = messageArguments;
        newContext->method            = method;
        newContext->previousContext   = previousContext;
    }
    
    try {
        TObject* result = compiledMethodFunction(newContext);
        return result;
    } catch( ... ) {
        //FIXME
        //Do not remove this try catch (you will get "terminate called after throwing an instance of 'TContext*' or 'TBlockReturn'")
        throw;
    }
}

void JITRuntime::updateHotSites(TMethodFunction methodFunction, TContext* callingContext, TSymbol* message, TClass* receiverClass, uint32_t callSiteIndex) 
{
    THotMethod& hotMethod = m_hotMethods[methodFunction];
    hotMethod.hitCount += 1;
        
    if (!callSiteIndex)
        return;
    
//     if (callingContext->getClass() == globals.blockClass)
//         static_cast<TBlock*>(callingContext)->;
    
    TMethodFunction callerMethodFunction = lookupFunctionInCache(callingContext->method);
    // TODO reload cache if callerMethodFunction was popped out
    
    THotMethod& callerMethod = m_hotMethods[callerMethodFunction];
    TCallSite& callSite = callerMethod.callSites[callSiteIndex];
    
    if (!callSite.hitCount)
        callSite.messageSelector = message;
    callSite.hitCount += 1;
    
    // Collecting statistics of receiver classes that involved in this message
    callSite.classHits[receiverClass] += 1;
}

void JITRuntime::patchHotMethods() 
{
    // Selecting most active methods with call sites and patching them.
    // We collected statistics on what classes are affected when invoking
    // particular site. Now we may hard code the check into method's code
    // to take advantage of direct method call and allow LLVM to do its job.
    
    // Allocating pointer array which will hold methods sorted by hits count
    std::vector<THotMethod*> hotMethods;
    THotMethodsMap::iterator iMethod = m_hotMethods.begin();
    for (; iMethod != m_hotMethods.end(); ++iMethod)
        hotMethods.push_back(& iMethod->second);
    
    // Sorting method pointers by hitCount field
    std::sort(hotMethods.begin(), hotMethods.end(), compareByHitCount);

    outs() << "Patching active methods that have hot call sites\n";
    
    // Processing 50 most active methods
    for (uint32_t i = 0, j = hotMethods.size()-1; (i < 50) && (i < hotMethods.size()); i++, j--) {
        THotMethod* hotMethod = hotMethods[j];
        
        // We're interested only in methods with call sites
        if (hotMethod->callSites.empty())
            continue;
        
        TMethod* method = hotMethod->method;
        Function* methodFunction = hotMethod->methodFunction;
        if (!method || !methodFunction)
            continue;
        
        // Cleaning up the function
        m_executionEngine->freeMachineCodeForFunction(methodFunction);
        methodFunction->getBasicBlockList().clear();
        
        // Compiling function from scratch
        outs() << "Recompiling method for patching: " << methodFunction->getName().str() << "\n";
        Value* contextHolder = 0;
        m_methodCompiler->compileMethod(method, methodFunction, &contextHolder);
        
        outs() << "Patching " << hotMethod->methodFunction->getName().str() << " ...";
        
        // Iterating through call sites and inserting class checks with direct calls
        THotMethod::TCallSiteMap::iterator iSite = hotMethod->callSites.begin();
        while (iSite != hotMethod->callSites.end()) {
            patchCallSite(hotMethod->methodFunction, contextHolder, iSite->second, iSite->first);
            ++iSite;
        }

//         outs() << "Patched code: \n" << *hotMethod->methodFunction << "\n";
        
        outs() << "done. Verifying ...";
        
        verifyModule(*m_JITModule, AbortProcessAction);
        
        outs() << "done.\n";
        
    }

    // Running optimization passes on functions
    for (uint32_t i = 0, j = hotMethods.size()-1; (i < 50) && (i < hotMethods.size()); i++, j--) {
        THotMethod* hotMethod = hotMethods[j];
        
        // We're interested only in methods with call sites
        if (hotMethod->callSites.empty())
            continue;
        
        TMethod* method = hotMethod->method;
        Function* methodFunction = hotMethod->methodFunction;
        if (!method || !methodFunction)
            continue;
        
        outs() << "Optimizing " << hotMethod->methodFunction->getName().str() << " ...";
        optimizeFunction(hotMethod->methodFunction);
        
        outs() << "done. Verifying ...";
        
        verifyModule(*m_JITModule, AbortProcessAction);
        
//         outs() << "Optimized code: \n" << *hotMethod->methodFunction;
        
        outs() << "done.\n";
    }
        
    // Compiling functions
    for (uint32_t i = 0, j = hotMethods.size()-1; (i < 50) && (i < hotMethods.size()); i++, j--) {
        THotMethod* hotMethod = hotMethods[j];
        
        // We're interested only in methods with call sites
        if (hotMethod->callSites.empty())
            continue;
        
        TMethod* method = hotMethod->method;
        Function* methodFunction = hotMethod->methodFunction;
        if (!method || !methodFunction)
            continue;
        
        outs() << "Compiling machine code for " << hotMethod->methodFunction->getName().str() << " ...";
        m_executionEngine->recompileAndRelinkFunction(hotMethod->methodFunction);
        
        
//         outs() << "Final code: \n" << *hotMethod->methodFunction;
        
        outs() << "done.\n";
    }
    
    // Invalidating caches
    std::memset(&m_blockFunctionLookupCache, 0, sizeof(m_blockFunctionLookupCache));
    std::memset(&m_functionLookupCache, 0, sizeof(m_functionLookupCache));
    
    outs() << "All is done.\n";
}

llvm::Instruction* JITRuntime::findCallInstruction(llvm::Function* methodFunction, uint32_t callSiteIndex) 
{
    using namespace llvm;
    
    Value* callOffsetValue = ConstantInt::get(Type::getInt32Ty(getGlobalContext()), callSiteIndex);
    
    // Locating call site instruction through all basic blocks
    for (Function::iterator iBlock = methodFunction->begin(); iBlock != methodFunction->end(); ++iBlock)
        for (BasicBlock::iterator iInst = iBlock->begin(); iInst != iBlock->end(); ++iInst)
            if (isa<CallInst>(iInst) || isa<InvokeInst>(iInst)) {
                CallSite call(iInst);
                
                if (call.getCalledFunction() == m_runtimeAPI.sendMessage && call.getArgument(4) == callOffsetValue)
                    return iInst;
            }
    // Instruction was not found (probably due to optimization)
    return 0;
}

void JITRuntime::createDirectBlocks(TPatchInfo& info, TCallSite& callSite, TDirectBlockMap& directBlocks) 
{
    using namespace llvm;
    
    IRBuilder<> builder(info.callInstruction->getParent());
    TCallSite::TClassHitsMap& classHits = callSite.classHits;

    // FIXME Probably we need to select only N most active class hits
    for (TCallSite::TClassHitsMap::iterator iClassHit = classHits.begin(); iClassHit != classHits.end(); ++iClassHit) {
        TDirectBlock newBlock;
        newBlock.basicBlock = BasicBlock::Create(m_JITModule->getContext(), "direct.", info.callInstruction->getParent()->getParent(), info.nextBlock);
        
        builder.SetInsertPoint(newBlock.basicBlock);
        
        // Locating a method suitable for a direct call
        TMethod* directMethod = m_softVM->lookupMethod(callSite.messageSelector, iClassHit->first); // TODO check for 0
        std::string directFunctionName = directMethod->klass->name->toString() + ">>" + callSite.messageSelector->toString();
        Function* directFunction = m_JITModule->getFunction(directFunctionName);
        
        if (!directFunction) {
            outs() << "Error! Could not acquire direct function for name " << directFunctionName << "\n";
            abort();
        }
        
//         FunctionType* _printfType = FunctionType::get(builder.getInt32Ty(), builder.getInt8PtrTy(), true);
//         Constant*     _printf     = m_JITModule->getOrInsertFunction("printf", _printfType);
//         Value* debugFormat = builder.CreateGlobalStringPtr("direct method '%s' : %d\n");
//         Value* strName     = builder.CreateGlobalStringPtr(directFunctionName);
//         builder.CreateCall3(_printf, debugFormat,  strName, builder.getInt32(0));
        
        // Allocating context object and temporaries on the methodFunction's stack.
        // This operation does not affect garbage collector, so no pointer protection
        // is required. Moreover, this is operation is much faster than heap allocation.
        const bool hasTemporaries  = getIntegerValue(directMethod->temporarySize) > 0;
        const uint32_t contextSize = sizeof(TContext);
        const uint32_t tempsSize   = hasTemporaries ? sizeof(TObjectArray) + sizeof(TObject*) * getIntegerValue(directMethod->temporarySize) : 0;
        
        // Allocating stack space for objects and registering GC protection holder
        
        MethodCompiler::TStackObject contextPair = m_methodCompiler->allocateStackObject(builder, sizeof(TContext), 0);
        Value* contextSlot = contextPair.objectSlot;
        newBlock.contextHolder = contextPair.objectHolder;
        
        Value* tempsSlot = 0;
        if (hasTemporaries) {
            MethodCompiler::TStackObject tempsPair = m_methodCompiler->allocateStackObject(builder, sizeof(TObjectArray), getIntegerValue(directMethod->temporarySize));
            tempsSlot = tempsPair.objectSlot;
            newBlock.tempsHolder = tempsPair.objectHolder;
        } else
            newBlock.tempsHolder = 0;
        
        // Filling stack space with zeroes
        builder.CreateMemSet(
            contextSlot,            // destination address
            builder.getInt8(0),     // fill with zeroes
            contextSize,            // size of object slot
            0,                      // no alignment
            false                   // volatile operation
        );
        
        if (hasTemporaries)
            builder.CreateMemSet(
                tempsSlot,           // destination address
                builder.getInt8(0),  // fill with zeroes
                tempsSize,           // size of object slot
                0,                   // no alignment
                false                // volatile operation
            );
        
        // Initializing object fields
        // TODO Move the init sequence out of the direct block or check that it is correctly optimized in loops
        Value* newContextObject  = builder.CreateBitCast(contextSlot, m_baseTypes.object->getPointerTo(), "newContext.");
        Value* newTempsObject    = hasTemporaries ? builder.CreateBitCast(tempsSlot, m_baseTypes.object->getPointerTo(), "newTemps.") : 0;
        Function* setObjectSize  = m_methodCompiler->getBaseFunctions().setObjectSize;
        Function* setObjectClass = m_methodCompiler->getBaseFunctions().setObjectClass;
        
        // Object size stored in the TSize field of any ordinary object contains
        // number of pointers except for the first two fields
        const uint32_t contextFieldsCount = contextSize / sizeof(TObject*) - 2;
        
        builder.CreateCall2(setObjectSize, newContextObject, builder.getInt32(contextFieldsCount));
        builder.CreateCall2(setObjectClass, newContextObject, m_methodCompiler->getJitGlobals().contextClass);
        
        if (hasTemporaries) {
            const uint32_t tempsFieldsCount = tempsSize / sizeof(TObject*) - 2;
            builder.CreateCall2(setObjectSize, newTempsObject, builder.getInt32(tempsFieldsCount));
            builder.CreateCall2(setObjectClass, newTempsObject, m_methodCompiler->getJitGlobals().arrayClass);
        }
        
        Function* setObjectField  = m_methodCompiler->getBaseFunctions().setObjectField;
        Value* methodRawPointer   = builder.getInt32(reinterpret_cast<uint32_t>(directMethod));
        Value* directMethodObject = builder.CreateIntToPtr(methodRawPointer, m_baseTypes.object->getPointerTo());
        
        Value* previousContext = builder.CreateLoad(info.contextHolder);
        Value* contextObject   = builder.CreateBitCast(previousContext, m_baseTypes.object->getPointerTo());
        Value* messageArgumentsObject = builder.CreateBitCast(info.messageArguments, m_baseTypes.object->getPointerTo());
        
        builder.CreateCall3(setObjectField, newContextObject, builder.getInt32(0), directMethodObject);
        builder.CreateCall3(setObjectField, newContextObject, builder.getInt32(1), messageArgumentsObject);
        if (hasTemporaries)
            builder.CreateCall3(setObjectField, newContextObject, builder.getInt32(2), newTempsObject);
        else
            builder.CreateCall3(setObjectField, newContextObject, builder.getInt32(2), m_methodCompiler->getJitGlobals().nilObject);
        builder.CreateCall3(setObjectField, newContextObject, builder.getInt32(3), contextObject);
        
        Value* newContext = builder.CreateBitCast(newContextObject, m_baseTypes.context->getPointerTo());
        // Creating direct version of a call
        if (isa<CallInst>(info.callInstruction)) {
            newBlock.returnValue = builder.CreateCall(directFunction, newContext);
            builder.CreateBr(info.nextBlock);
        } else {
            InvokeInst* invokeInst = dyn_cast<InvokeInst>(info.callInstruction);
            newBlock.returnValue = builder.CreateInvoke(directFunction, 
                                                        info.nextBlock,
                                                        invokeInst->getUnwindDest(),
                                                        newContext
            );
        }
        newBlock.returnValue->setName("reply.");
        
        directBlocks[iClassHit->first] = newBlock;
    }
}

void JITRuntime::cleanupDirectHolders(llvm::IRBuilder<>& builder, TDirectBlock& directBlock) 
{
    builder.CreateStore(ConstantPointerNull::get(m_baseTypes.object->getPointerTo()), directBlock.contextHolder/*, true*/);
    if (directBlock.tempsHolder)
        builder.CreateStore(ConstantPointerNull::get(m_baseTypes.object->getPointerTo()), directBlock.tempsHolder/*, true*/);
}

bool JITRuntime::detectLiteralReceiver(llvm::Value* messageArguments)
{
    Value* args = messageArguments->stripPointerCasts();
    
    CallInst* createArgsCall = 0;
    if (isa<CallInst>(args)) {
        createArgsCall = cast<CallInst>(args);
    } else if (isa<LoadInst>(args)) {
        Value* argsHolder = cast<LoadInst>(args)->getPointerOperand();
        
        for(Value::use_iterator use = argsHolder->use_begin(); use != argsHolder->use_end(); ++use) {
            if (StoreInst* storeToArgsHolder = dyn_cast<StoreInst>(*use)) {
                Value* createArgs = storeToArgsHolder->getValueOperand()->stripPointerCasts();
                createArgsCall = dyn_cast<CallInst>(createArgs);
                if (createArgsCall != 0)
                    break;
            }
        }
    }
    
    if (!createArgsCall)
        return false;
    
    if (createArgsCall->getCalledFunction() != m_methodCompiler->getRuntimeAPI().newOrdinaryObject)
        return false;
    
    Value* receiver = 0; // receiver == args[0]
    
    Function* setObjectField = m_methodCompiler->getBaseFunctions().setObjectField;
    for(Value::use_iterator use = createArgsCall->use_begin(); use != createArgsCall->use_end(); ++use) {
        if (CallInst* call = dyn_cast<CallInst>(*use)) {
            if (call->getCalledFunction() != setObjectField)
                continue;
            
            Value* arg0 = call->getArgOperand(0);
            ConstantInt* zeroIndex = ConstantInt::get(Type::getInt32Ty(getGlobalContext()), 0);
            if ( createArgsCall->isIdenticalTo(cast<CallInst>(arg0)) && call->getArgOperand(1) == zeroIndex )
            {
                receiver = call->getArgOperand(2);
                break;
            }
        }
    }
    
    if (!receiver) {
        //TODO try to find receiver in GEPs
        return false;
    } 
    
    if (isa<ConstantExpr>(receiver)) {
        // inlined SmallInt
        return true;
    }
    
    if (CallInst* call = dyn_cast<CallInst>(receiver)) {
        if (call->getCalledFunction() == m_methodCompiler->getBaseFunctions().getLiteral)
            return true;
    }
    
    return false;
}

void JITRuntime::patchCallSite(llvm::Function* methodFunction, llvm::Value* contextHolder, TCallSite& callSite, uint32_t callSiteIndex) 
{
    using namespace llvm;
    
    Instruction* callInstruction = findCallInstruction(methodFunction, callSiteIndex);
    
    if (! callInstruction)
        return; // Seems that instruction was completely optimized out
    
    CallSite call(callInstruction);
    
    
    BasicBlock* originBlock = callInstruction->getParent();
    IRBuilder<> builder(originBlock);
    
    // Spliting original block into two parts. 
    // These will be intersected by our code
    BasicBlock* nextBlock = originBlock->splitBasicBlock(callInstruction, "next.");
    
    // Now, preparing a set of basic blocks with direct calls to class methods
    TDirectBlockMap directBlocks;
    TPatchInfo info;
    info.callInstruction = callInstruction;
    info.nextBlock = nextBlock;
    info.messageArguments = call.getArgument(2);
    info.contextHolder = contextHolder;
    createDirectBlocks(info, callSite, directBlocks);
    
    // Cutting unconditional branch between parts
    originBlock->getInstList().pop_back();

    // Checking whether we may perform constant class propagation
    bool isLiteralReceiver = detectLiteralReceiver(info.messageArguments) && (directBlocks.size() == 1);
    
    if (! isLiteralReceiver) {
        // Fallback block contains original call to default JIT sendMessage handler.
        // It is called when message is invoked with an unknown class that does not 
        // has direct handler.
        BasicBlock* fallbackBlock = BasicBlock::Create(callInstruction->getContext(), "fallback.", methodFunction, nextBlock);
        Value* fallbackReply = 0;
        
        builder.SetInsertPoint(fallbackBlock);
        
        SmallVector<Value*, 5> fallbackArgs;
        fallbackArgs.append(call.arg_begin(), call.arg_end());
        if (isa<CallInst>(callInstruction)) {
            fallbackReply = builder.CreateCall(call.getCalledFunction(), fallbackArgs, "reply.");
            builder.CreateBr(nextBlock);
        } else {
            InvokeInst* invokeInst = dyn_cast<InvokeInst>(callInstruction);
            fallbackReply = builder.CreateInvoke(invokeInst->getCalledFunction(), 
                                                 nextBlock,
                                                 invokeInst->getUnwindDest(),
                                                 fallbackArgs,
                                                 "reply."
            );
        }
    
        // Creating a switch instruction that will filter the block 
        // depending on the actual class of the receiver object
        builder.SetInsertPoint(originBlock);
        
        // Acquiring receiver's class pointer as raw int value
        Value* argumentsObject = call.getArgument(2);
        Value* arguments = builder.CreateBitCast(argumentsObject, m_baseTypes.object->getPointerTo());
        
        Function* getObjectField = m_methodCompiler->getBaseFunctions().getObjectField;
        Function* getObjectClass = m_methodCompiler->getBaseFunctions().getObjectClass;
        Value* receiver = builder.CreateCall2(getObjectField, arguments, builder.getInt32(0));
        Value* receiverClass = builder.CreateCall(getObjectClass, receiver);
        Value* receiverClassPtr = builder.CreatePtrToInt(receiverClass, Type::getInt32Ty(getGlobalContext()));

        // Genrating switch instruction to select basic block
        SwitchInst* switchInst = builder.CreateSwitch(receiverClassPtr, fallbackBlock);
        
        // This phi function will aggregate direct block and fallback block return values
        builder.SetInsertPoint(nextBlock, nextBlock->getInstList().begin());
        PHINode* replyPhi = builder.CreatePHI(m_baseTypes.object->getPointerTo(), 2, "phi.");
        replyPhi->addIncoming(fallbackReply, fallbackBlock);
        
        // Splitting original block tore execution flow. Reconnecting blocks
        if (InvokeInst* invokeInst = dyn_cast<InvokeInst>(callInstruction)) {
            BranchInst* branch = builder.CreateBr(invokeInst->getNormalDest());
            
            // Setting up builder for cleanup opertions
            builder.SetInsertPoint(branch);
        }
        
        for (TDirectBlockMap::iterator iBlock = directBlocks.begin(); iBlock != directBlocks.end(); ++iBlock)  {
            TClass* klass = iBlock->first;
            TDirectBlock& directBlock = iBlock->second;
            
            ConstantInt* classAddress = builder.getInt32(reinterpret_cast<uint32_t>(klass));
            switchInst->addCase(classAddress, directBlock.basicBlock);
            
            replyPhi->addIncoming(directBlock.returnValue, directBlock.basicBlock);
            
            // Adding cleanup code for the direct block
            cleanupDirectHolders(builder, directBlock);
        }    
        
        callInstruction->replaceAllUsesWith(replyPhi);
    } else {
        // Literal receivers are constants that are written 
        // at method compilation time and do not change.
        
        // We may take advantage of this fact and optimize call 
        // of method with literal object as a receiver. Because 
        // literal object is a constant, it's class is constant too.
        // Therefore, we do not need to check it every time we call a method.
        
        TDirectBlock& directBlock = (*directBlocks.begin()).second;
        
        builder.SetInsertPoint(originBlock);
        builder.CreateBr(directBlock.basicBlock);
        callInstruction->replaceAllUsesWith(directBlock.returnValue);
    }
    
    callInstruction->eraseFromParent();
}

void JITRuntime::initializeGlobals() {
    GlobalValue* m_jitGlobals = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("globals", m_baseTypes.globals) );
    m_executionEngine->addGlobalMapping(m_jitGlobals, reinterpret_cast<void*>(&globals));
    
    GlobalValue* gNil = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("nilObject", m_baseTypes.object) );
    m_executionEngine->addGlobalMapping(gNil, reinterpret_cast<void*>(globals.nilObject));
    
    GlobalValue* gTrue = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("trueObject", m_baseTypes.object) );
    m_executionEngine->addGlobalMapping(gTrue, reinterpret_cast<void*>(globals.trueObject));
    
    GlobalValue* gFalse = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("falseObject", m_baseTypes.object) );
    m_executionEngine->addGlobalMapping(gFalse, reinterpret_cast<void*>(globals.falseObject));
    
    GlobalValue* gSmallIntClass = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("SmallInt", m_baseTypes.klass) );
    m_executionEngine->addGlobalMapping(gSmallIntClass, reinterpret_cast<void*>(globals.smallIntClass));
    
    GlobalValue* gArrayClass = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("Array", m_baseTypes.klass) );
    m_executionEngine->addGlobalMapping(gArrayClass, reinterpret_cast<void*>(globals.arrayClass));
    
    GlobalValue* gContextClass = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("Context", m_baseTypes.klass) );
    m_executionEngine->addGlobalMapping(gContextClass, reinterpret_cast<void*>(globals.contextClass));
    
    GlobalValue* gmessageL = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("<", m_baseTypes.symbol) );
    m_executionEngine->addGlobalMapping(gmessageL, reinterpret_cast<void*>(globals.binaryMessages[0]));
    
    GlobalValue* gmessageLE = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("<=", m_baseTypes.symbol) );
    m_executionEngine->addGlobalMapping(gmessageLE, reinterpret_cast<void*>(globals.binaryMessages[1]));
    
    GlobalValue* gmessagePlus = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("+", m_baseTypes.symbol) );
    m_executionEngine->addGlobalMapping(gmessagePlus, reinterpret_cast<void*>(globals.binaryMessages[2]));
}

void JITRuntime::initializePassManager() {
    m_functionPassManager = new FunctionPassManager(m_JITModule);
    m_modulePassManager   = new PassManager();
    // Set up the optimizer pipeline.
    // Start with registering info about how the
    // target lays out data structures.
    m_functionPassManager->add(new TargetData(*m_executionEngine->getTargetData()));
    
    m_modulePassManager->add(createFunctionInliningPass());
    
    m_functionPassManager->add(createBasicAliasAnalysisPass());
    m_functionPassManager->add(createPromoteMemoryToRegisterPass());
    m_functionPassManager->add(createInstructionCombiningPass());
    m_functionPassManager->add(createReassociatePass());
    m_functionPassManager->add(createGVNPass());
    m_functionPassManager->add(createAggressiveDCEPass());
    m_functionPassManager->add(createTailCallEliminationPass());
    m_functionPassManager->add(createCFGSimplificationPass());
    m_functionPassManager->add(createDeadCodeEliminationPass());
    m_functionPassManager->add(createDeadStoreEliminationPass());
    
    m_functionPassManager->add(createLLSTPass()); // FIXME direct calls break the logic
//     //If llstPass removed GC roots, we may try DCE again
    m_functionPassManager->add(createDeadCodeEliminationPass());
    m_functionPassManager->add(createDeadStoreEliminationPass());
    
    //m_functionPassManager->add(createLLSTDebuggingPass());
    m_modulePassManager->add(createFunctionInliningPass());
    m_functionPassManager->doInitialization();
}

void JITRuntime::initializeRuntimeAPI() {
    // Creating function references
    m_runtimeAPI.newOrdinaryObject  = m_JITModule->getFunction("newOrdinaryObject");
    m_runtimeAPI.newBinaryObject    = m_JITModule->getFunction("newBinaryObject");
    m_runtimeAPI.sendMessage        = m_JITModule->getFunction("sendMessage");
    m_runtimeAPI.createBlock        = m_JITModule->getFunction("createBlock");
    m_runtimeAPI.invokeBlock        = m_JITModule->getFunction("invokeBlock");
    m_runtimeAPI.emitBlockReturn    = m_JITModule->getFunction("emitBlockReturn");
    m_runtimeAPI.checkRoot          = m_JITModule->getFunction("checkRoot");
    m_runtimeAPI.bulkReplace        = m_JITModule->getFunction("bulkReplace");
    m_runtimeAPI.callPrimitive      = m_JITModule->getFunction("callPrimitive");
    
    // Mapping the function references to actual functions
    m_executionEngine->addGlobalMapping(m_runtimeAPI.newOrdinaryObject, reinterpret_cast<void*>(& ::newOrdinaryObject));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.newBinaryObject, reinterpret_cast<void*>(& ::newBinaryObject));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.sendMessage, reinterpret_cast<void*>(& ::sendMessage));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.createBlock, reinterpret_cast<void*>(& ::createBlock));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.invokeBlock, reinterpret_cast<void*>(& ::invokeBlock));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.emitBlockReturn, reinterpret_cast<void*>(& ::emitBlockReturn));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.checkRoot, reinterpret_cast<void*>(& ::checkRoot));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.bulkReplace, reinterpret_cast<void*>(& ::bulkReplace));
    m_executionEngine->addGlobalMapping(m_runtimeAPI.callPrimitive, reinterpret_cast<void*>(& ::callPrimitive));
    
    //Type*  rootChainType = m_JITModule->getTypeByName("gc_stackentry")->getPointerTo();
    //GlobalValue* gRootChain    = cast<GlobalValue>( m_JITModule->getOrInsertGlobal("llvm_gc_root_chain", rootChainType) );
    GlobalValue* gRootChain = m_JITModule->getGlobalVariable("llvm_gc_root_chain");
    m_executionEngine->addGlobalMapping(gRootChain, reinterpret_cast<void*>(&llvm_gc_root_chain));
}

void JITRuntime::initializeExceptionAPI() {
    m_exceptionAPI.gcc_personality = m_JITModule->getFunction("__gcc_personality_v0");
    m_exceptionAPI.cxa_begin_catch = m_JITModule->getFunction("__cxa_begin_catch");
    m_exceptionAPI.cxa_end_catch   = m_JITModule->getFunction("__cxa_end_catch");
    m_exceptionAPI.cxa_allocate_exception = m_JITModule->getFunction("__cxa_allocate_exception");
    m_exceptionAPI.cxa_throw       = m_JITModule->getFunction("__cxa_throw");
    
    LLVMContext& Context = m_JITModule->getContext();
    Type* Int8PtrTy      = Type::getInt8PtrTy(Context);
    
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
    Function* gcrootIntrinsic = getDeclaration(m_JITModule, Intrinsic::gcroot);
    builder.CreateCall2(gcrootIntrinsic, builder.CreateBitCast(processHolder, builder.getInt8PtrTy()->getPointerTo()), ConstantPointerNull::get(builder.getInt8PtrTy()) );
    builder.CreateStore(process, processHolder);
    
    Value* contextPtr  = builder.CreateStructGEP(process, 1);
    Value* context     = builder.CreateLoad(contextPtr);
    Value* argsPtr     = builder.CreateStructGEP(context, 2);
    Value* args        = builder.CreateLoad(argsPtr);
    Value* methodPtr   = builder.CreateStructGEP(context, 1);
    Value* method      = builder.CreateLoad(methodPtr);
    Value* selectorPtr = builder.CreateStructGEP(method, 1);
    Value* selector    = builder.CreateLoad(selectorPtr);
    Value* previousContextPtr = builder.CreateStructGEP(context, 7);
    Value* previousContext    = builder.CreateLoad(previousContextPtr);
    
    BasicBlock* OK   = BasicBlock::Create(m_JITModule->getContext(), "OK", executeProcess);
    BasicBlock* Fail = BasicBlock::Create(m_JITModule->getContext(), "FAIL", executeProcess);
    
    Value* sendMessageArgs[] = {
        previousContext,
        selector,
        args,
        ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()),
        builder.getInt32(0)
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
    Value* thrownContext    = builder.CreateLoad( builder.CreateBitCast(thrownException, m_baseTypes.context->getPointerTo()->getPointerTo()) );
    
    process = builder.CreateLoad(processHolder);
    contextPtr = builder.CreateStructGEP(process, 1);
    builder.CreateStore(thrownContext, contextPtr);
    
    builder.CreateCall(m_exceptionAPI.cxa_end_catch);
    builder.CreateRet( builder.getInt32(SmalltalkVM::returnError) );
}

extern "C" {

TObject* newOrdinaryObject(TClass* klass, uint32_t slotSize)
{
    JITRuntime::Instance()->m_objectsAllocated++;
    return JITRuntime::Instance()->getVM()->newOrdinaryObject(klass, slotSize);
}

TByteObject* newBinaryObject(TClass* klass, uint32_t dataSize)
{
    JITRuntime::Instance()->m_objectsAllocated++;
    return JITRuntime::Instance()->getVM()->newBinaryObject(klass, dataSize);
}

TObject* sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass, uint32_t callSiteIndex)
{
    JITRuntime::Instance()->m_messagesDispatched++;
    return JITRuntime::Instance()->sendMessage(callingContext, message, arguments, receiverClass, callSiteIndex);
}

TBlock* createBlock(TContext* callingContext, uint8_t argLocation, uint16_t bytePointer)
{
    return JITRuntime::Instance()->createBlock(callingContext, argLocation, bytePointer);
}

TObject* invokeBlock(TBlock* block, TContext* callingContext)
{
    JITRuntime::Instance()->m_blocksInvoked++;
    return JITRuntime::Instance()->invokeBlock(block, callingContext);
}

void emitBlockReturn(TObject* value, TContext* targetContext)
{
    throw TBlockReturn(value, targetContext);
}

void checkRoot(TObject* value, TObject** objectSlot)
{
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
