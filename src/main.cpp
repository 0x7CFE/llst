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

#include <iostream>
#include <stdio.h>
#include <memory>

#include <vm.h>

//#define test

#ifdef test

TChar* newChar(SmalltalkVM* vm, char c) {
    TChar* character = vm->newObject<TChar>(0, false);
    character->value = newInteger(c);
    return character;
}

void printSymbol(TChar* character) {
    putchar( getIntegerValue( character->value ));
}

TObjectArray* chars;
void initChars(SmalltalkVM* vm) {
    chars = vm->newObject<TObjectArray>(255);
    for(int i = 0; i < 255; i++)
        chars->putField(i, newChar(vm, i));
}

TObjectArray* newCharString(SmalltalkVM* vm, const char* str, int len) {
    hptr<TObjectArray> string = vm->newObject<TObjectArray>(len);
    for(int i = 0; i<len; i++) {
        string->putField(i, chars->getField(str[i]) );
    }
    return string;
}

TSymbolArray* newRandomString(SmalltalkVM* vm) {
    srand( time(0) );
    int len = rand() % 200;
    hptr<TSymbolArray> string = vm->newObject<TSymbolArray>(len);
    for(int i = 0; i < len; i++) {
        string->putField(i, chars->getField( (rand() % 10) + 51 ) );
    }
    return string;
}

void printString(TObjectArray* string) {
    for(uint i = 0; i<string->getSize(); i++) {
        printSymbol( (TChar*) string->getField(i) );
    }
}

#endif

int main(int argc, char **argv) {
    std::auto_ptr<IMemoryManager> memoryManager(new BakerMemoryManager());
    memoryManager->initializeHeap(65536 * 32);
    
    std::auto_ptr<Image> testImage(new Image(memoryManager.get()));
    if (argc == 2)
        testImage->loadImage(argv[1]);
    else
        testImage->loadImage("../image/testImage");
    
    SmalltalkVM vm(testImage.get(), memoryManager.get());
    
#ifdef test
    memoryManager->registerExternalPointer((TObject**) &chars);
    initChars(&vm);
    
    hptr<TObjectArray> stack = vm.newObject<TObjectArray>(5);
    
    // We create a string, copy it to stack[0], and put onto stack[2] a random string.
    
    stack[0] = globals.nilObject; // string will be copied here 
    stack[1] = newCharString(&vm, "Hello world!\n", 13);
    
    TByteArray* array = vm.newObject<TByteArray>(127);
    strcpy((char*) array->getBytes(), "Hello world from byte array!");
    stack[3] = array;
    
    printf("Before: %s\n", (const char*) stack[3]->getFields());
    getchar();
    
    for( int i = 0; i < 2000; i++) {
        TObject* string = stack[1];
        stack[0] = string;
        stack[2] = newRandomString(&vm);
    }
    
    printString( (TObjectArray*) stack[0] );
    printf("After: %s\n", (const char*) stack[3]->getFields());

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
            
        case SmalltalkVM::returnBadMethod:
            printf("Could not lookup method\n");
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

    TMemoryManagerInfo info = memoryManager->getStat();
    
    int averageAllocs = info.collectionsCount ? (int) info.allocationsCount / info.collectionsCount : info.allocationsCount;
    printf("\nGC count: %d, average allocations per gc: %d, microseconds spent in GC: %d\n", 
           info.collectionsCount, averageAllocs, (uint32_t) info.totalCollectionDelay);
    
    vm.printVMStat();

    return 0;
}
