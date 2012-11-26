#include <iostream>
#include <vm.h>
#include <stdio.h>

int main(int argc, char **argv) {
    //HeapMemoryManager staticHeap(80000);
    
    Image testImage(0);
    testImage.loadImage("../image/testImage");
    
    SmalltalkVM vm;
    
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

    
    
    //ptr<TSymbolArray, TSymbol*> 
//     PSymbolArray symbols = globals.globalsObject->keys;
//     TSymbol* symbol = symbols[1];
//     TSymbolArray& rsymbols = *symbols;
//     symbols[2] = (TSymbol*) globals.nilObject;
    //(temps.ref())[1] = globals.nilObject;
    
    SmalltalkVM::TExecuteResult result = vm.execute(initProcess, 0);
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
