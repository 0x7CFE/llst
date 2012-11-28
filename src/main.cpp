#include <iostream>
#include <stdio.h>
#include <memory>

#include <vm.h>

int main(int argc, char **argv) {
    std::auto_ptr<IMemoryManager> bakerMemoryManager(new BakerMemoryManager());
    bakerMemoryManager->initializeHeap(4096);
    
    std::auto_ptr<Image> testImage(new Image(bakerMemoryManager.get()));
    testImage->loadImage("../image/testImage");
    
    SmalltalkVM vm(testImage.get(), bakerMemoryManager.get());
    
    // Creating runtime context
    TProcess* initProcess = vm.newObject<TProcess>();
    TContext* initContext = vm.newObject<TContext>();
    
    initProcess->context = initContext;
    
    initContext->arguments = (TObjectArray*) globals.nilObject;
    initContext->temporaries = vm.newObject<TObjectArray>(20); //TODO
    initContext->bytePointer = newInteger(0);
    initContext->previousContext = (TContext*) globals.nilObject;
    
    uint32_t stackSize = getIntegerValue(globals.initialMethod->stackSize);
    initContext->stack = vm.newObject<TObjectArray>(stackSize);
    initContext->stackTop = newInteger(0);
    initContext->method = globals.initialMethod;
    
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
