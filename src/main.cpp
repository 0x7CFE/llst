#include <iostream>
#include <stdio.h>
#include <memory>

#include <vm.h>

int main(int argc, char **argv) {
    std::auto_ptr<IMemoryManager> memoryManager(new BakerMemoryManager());
    memoryManager->initializeHeap(65536);
    
    std::auto_ptr<Image> testImage(new Image(memoryManager.get()));
    testImage->loadImage("../image/testImage");
    
    SmalltalkVM vm(testImage.get(), memoryManager.get());
    
    // Creating runtime context
    TContext* initContext = vm.newObject<TContext>();
    TProcess* initProcess = vm.newObject<TProcess>();
    initProcess->context = initContext;
    
    initContext->arguments = (TObjectArray*) globals.nilObject;
    initContext->bytePointer = newInteger(0);
    initContext->previousContext = (TContext*) globals.nilObject;
    
    const uint32_t stackSize = getIntegerValue(globals.initialMethod->stackSize);
    initContext->stack = vm.newObject<TObjectArray>(stackSize);
    initContext->stackTop = newInteger(0);
    
    initContext->method = globals.initialMethod;
    initContext->temporaries = vm.newObject<TObjectArray>(initContext->method->temporarySize);
    
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
