/*
 *    main.cpp
 *
 *    Program entry point
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.2
 *
 *    LLST is
 *        Copyright (C) 2012-2013 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2013 by Roman Proskuryakov <humbug@deeptown.org>
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

#include <iostream>
#include <cstdio>
#include <memory>

#include <vm.h>
#include <args.h>

#include <CompletionEngine.h>

#if defined(LLVM)
    #include <jit.h>
#endif

int main(int argc, char **argv) {
    args llstArgs;

    llstArgs.heapSize = 1048576;
    llstArgs.maxHeapSize = 1048576 * 100;
    llstArgs.imagePath = "../image/LittleSmalltalk.image";

    llstArgs.parse(argc, argv);

    if (llstArgs.showHelp) {
        std::cout << args::getHelp() << std::endl;
        return EXIT_SUCCESS;
    }

#if defined(LLVM)
    std::auto_ptr<IMemoryManager> memoryManager(new LLVMMemoryManager());
#else
    std::auto_ptr<IMemoryManager> memoryManager(new BakerMemoryManager());
#endif
    memoryManager->initializeHeap(llstArgs.heapSize, llstArgs.maxHeapSize);

    std::auto_ptr<Image> smalltalkImage(new Image(memoryManager.get()));
    smalltalkImage->loadImage(llstArgs.imagePath);

    {
        Image::ImageWriter writer;
        writer.setGlobals(globals).writeTo("../image/MySmalltalkImage.image");
    }
    SmalltalkVM vm(smalltalkImage.get(), memoryManager.get());

    // Creating completion database and filling it with info
    CompletionEngine* completionEngine = CompletionEngine::Instance();
    completionEngine->initialize(globals.globalsObject);
    completionEngine->addWord("Smalltalk");

#if defined(LLVM)
    JITRuntime runtime;
    runtime.initialize(&vm);
#endif

    // Creating runtime context
    hptr<TContext> initContext = vm.newObject<TContext>();
    hptr<TProcess> initProcess = vm.newObject<TProcess>();
    initProcess->context = initContext;

    initContext->arguments = vm.newObject<TObjectArray>(1);
    initContext->arguments->putField(0, globals.nilObject);

    initContext->bytePointer = 0;
    initContext->previousContext = static_cast<TContext*>(globals.nilObject);

    const uint32_t stackSize = globals.initialMethod->stackSize;
    initContext->stack = vm.newObject<TObjectArray>(stackSize);
    initContext->stackTop = 0;

    initContext->method = globals.initialMethod;

    // FIXME image builder does not calculate temporary size
    //uint32_t tempsSize = getIntegerValue(initContext->method->temporarySize);
    initContext->temporaries = vm.newObject<TObjectArray>(42);

    // And starting the image execution!
    SmalltalkVM::TExecuteResult result = vm.execute(initProcess, 0);

    //llvm::outs() << *runtime.getModule();

    /* This code will run Smalltalk immediately in LLVM.
     * Don't forget to uncomment 'Undefined>>boot'
     */
    /*
    typedef int32_t (*TExecuteProcessFunction)(TProcess*);
    TExecuteProcessFunction executeProcess = reinterpret_cast<TExecuteProcessFunction>(runtime.getExecutionEngine()->getPointerToFunction(runtime.getModule()->getFunction("executeProcess")));
    SmalltalkVM::TExecuteResult result = (SmalltalkVM::TExecuteResult) executeProcess(initProcess);
    */
    // Finally, parsing the result
    switch (result) {
        case SmalltalkVM::returnError:
            std::printf("User defined return\n");
            break;

        case SmalltalkVM::returnBadMethod:
            std::printf("Could not lookup method\n");
            break;

        case SmalltalkVM::returnReturned:
            // normal return
            std::printf("Exited normally\n");
            break;

        case SmalltalkVM::returnTimeExpired:
            std::printf("Execution time expired\n");
            break;

        default:
            std::printf("Unknown return code: %d\n", result);

    }

    TMemoryManagerInfo info = memoryManager->getStat();

    int averageAllocs = info.collectionsCount ? info.allocationsCount / info.collectionsCount : info.allocationsCount;
    std::printf("\nGC count: %d (%d/%d), average allocations per gc: %d, microseconds spent in GC: %d\n",
           info.collectionsCount, info.leftToRightCollections, info.rightToLeftCollections, averageAllocs, static_cast<uint32_t>(info.totalCollectionDelay));

    vm.printVMStat();

#if defined(LLVM)
    runtime.printStat();
#endif

    return EXIT_SUCCESS;
}
