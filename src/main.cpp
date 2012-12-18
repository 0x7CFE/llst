#include <iostream>
#include <stdio.h>
#include <memory>

#include <vm.h>

#define test

#ifdef test

TSymbol* newSymbol(SmalltalkVM* vm, char c) {
    hptr<TSymbol> symbol = vm->newObject<TSymbol>(1);
    symbol->putByte(0, newInteger(c));
    return symbol;
}

void printSymbol(TSymbol* symbol) {
    putchar( getIntegerValue( symbol->getByte(0) ));
}

TSymbolArray* chars;
void initChars(SmalltalkVM* vm) {
    chars = vm->newObject<TSymbolArray>(255);
    for(int i = 0; i < 255; i++)
        chars->putField(i, newSymbol(vm, i));
}

TSymbolArray* newString(SmalltalkVM* vm, const char* str, int len) {
    hptr<TSymbolArray> string = vm->newObject<TSymbolArray>(len);
    for(int i = 0; i<len; i++) {
        string->putField(i, chars->getField(str[i]) );
    }
    return string;
}

TSymbolArray* newRandomString(SmalltalkVM* vm) {
    srand( time(0) );
    int len = 128; //rand() % 200;
    hptr<TSymbolArray> string = vm->newObject<TSymbolArray>(len);
    for(int i = 0; i < len; i++) {
        string->putField(i, chars->getField( (rand() % 10) + 51 ) );
    }
    return string;
}

void printString(TSymbolArray* string) {
    for(uint i = 0; i<string->getSize(); i++) {
        printSymbol( string->getField(i) );
    }
}

#endif

int main(int argc, char **argv) {
    std::auto_ptr<IMemoryManager> memoryManager(new BakerMemoryManager());
    memoryManager->initializeHeap(65536 * 4);
    
    std::auto_ptr<Image> testImage(new Image(memoryManager.get()));
    if (argc == 2)
        testImage->loadImage(argv[1]);
    else
        testImage->loadImage("../image/testImage");
    
    SmalltalkVM vm(testImage.get(), memoryManager.get());
    
#ifdef test
    initChars(&vm);
    
    hptr<TObjectArray> stack = vm.newObject<TObjectArray>(5);
    
    // We create a string, copy it to stack[0], and put onto stack[2] a random string.
    
    stack[0] = globals.nilObject; // string will be copied here 
    stack[1] = newString(&vm, "Hello world!\n", 13);
    for( int i = 0; i < 2000; i++) {
        TObject* string = stack[1];
        stack[0] = string;
        stack[2] = newRandomString(&vm);
    }
    
    printString( (TSymbolArray*) stack[0] );

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

    vm.pushProcess(initProcess);
    
    // And starting the image execution!
    SmalltalkVM::TExecuteResult result = vm.execute(initProcess, 0);
    
    // Finally, parsing the result
    switch (result) {
        case SmalltalkVM::returnError:
            printf("User defined return\n");
            break;
            
        case SmalltalkVM::returnReturned:
            // normal return
            printf("Exited normally\n");
            break;
            
        case SmalltalkVM::returnTimeExpired: 
            printf("Execution time expired\n");
            break;
            
        default:
            printf("Unknown return code: %d\n", result);
            
    }
#endif    

    vm.printVMStat();

    return 0;
}
