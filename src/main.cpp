#include <iostream>
#include <stdio.h>
#include <memory>

#include <vm.h>

int main(int argc, char **argv) {
    std::auto_ptr<IMemoryManager> memoryManager(new BakerMemoryManager());
    memoryManager->initializeHeap(65536 * 16);
    
    std::auto_ptr<Image> testImage(new Image(memoryManager.get()));
    testImage->loadImage("../image/testImage");
    
    SmalltalkVM vm(testImage.get(), memoryManager.get());
    
//     TSymbol* symbolA = vm.newObject<TSymbol>(10);
//     hptr<TSymbol> symbolB = vm.newObject<TSymbol>(10);
//     
//     symbolA->putByte(0, 42);
//     symbolB->putByte(0, 42);
//     
//     printf("symbolA ptr is %p, data %d\n", (TSymbol*) symbolA, symbolA->getByte(0));
//     printf("symbolB ptr is %p, data %d\n", (TSymbol*) symbolB, symbolB->getByte(0));
//     
//     memoryManager->collectGarbage();
//     
//     symbolA->putByte(1, 16);
//     symbolB->putByte(1, 16);
//     
//     printf("symbolA ptr is %p, data %d %d\n", (TSymbol*) symbolA, symbolA->getByte(0), symbolA->getByte(1));
//     printf("symbolB ptr is %p, data %d %d\n", (TSymbol*) symbolB, symbolB->getByte(0), symbolB->getByte(1));
    
    // Creating runtime context
    hptr<TContext> initContext = vm.newObject<TContext>();
    hptr<TProcess> initProcess = vm.newObject<TProcess>();
    initProcess->context = initContext;
    
    initContext->arguments = (TObjectArray*) globals.nilObject;
    initContext->bytePointer = newInteger(0);
    initContext->previousContext = (TContext*) globals.nilObject;
    
    const uint32_t stackSize = getIntegerValue(globals.initialMethod->stackSize);
    initContext->stack = vm.newObject<TObjectArray>(stackSize, false);
    initContext->stackTop = newInteger(0);
    
    initContext->method = globals.initialMethod;
    
    // TODO load value from 
    //uint32_t tempsSize = getIntegerValue(initContext->method->temporarySize);
    initContext->temporaries = vm.newObject<TObjectArray>(42, false);
    
    // And starting the image execution!
    SmalltalkVM::TExecuteResult result = vm.execute(initProcess, 0);
    
    // Finally, parsing the result
    switch (result) {
        case SmalltalkVM::returnError:
            printf("User defined return");
            break;
            
        case SmalltalkVM::returnReturned:
            // normal return
            printf("Exited normally");
            break;
            
        case SmalltalkVM::returnTimeExpired: 
            printf("Execution time expired");
            break;
            
        default:
            printf("Unknown return code: %d", result);
            
    }
    
    return 0;
}
