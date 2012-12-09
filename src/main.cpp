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

// #define test    
    
#ifdef test
    hptr<TDictionary> dict = vm.newObject<TDictionary>();
    dict->keys = (TSymbolArray*) vm.newObject<TSymbolArray>(1, false);
    (*dict->keys)[0] = vm.newObject<TSymbol>(1);
    (*dict->keys)[0]->putByte(0, 42);
    
    printf("dict.target      = %p\n", dict.rawptr());
    printf("dict->keys       = %p\n", dict->keys);
    printf("dict->keys[0]    = %p\n", dict->keys->getField(0));
    printf("dict->keys[0][0] = %d\n", dict->keys->getField(0)->getByte(0));
    
    memoryManager->collectGarbage();
    
    printf("dict.target      = %p\n", dict.rawptr());
    printf("dict->keys       = %p\n", dict->keys);
    printf("dict->keys[0]    = %p\n", dict->keys->getField(0));
    printf("dict->keys[0][0] = %d\n", dict->keys->getField(0)->getByte(0));
#else    
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
#endif    

    return 0;
}
