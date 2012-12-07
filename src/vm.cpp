#include <vm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

TObject* SmalltalkVM::newOrdinaryObject(TClass* klass, size_t slotSize)
{
    //ec.push(ec);
    
    void* objectSlot = m_memoryManager->allocate(slotSize, &m_lastGCOccured);
    if (!objectSlot)
        return globals.nilObject;
    
    if (m_lastGCOccured)
        onCollectionOccured();
    
    // Object size stored in the TSize field of any ordinary object contains
    // number of pointers except for the first two fields           
    size_t fieldsCount = slotSize / sizeof(TObject*) - 2;
    
    TObject* instance = new (objectSlot) TObject(fieldsCount, klass);

    for (uint32_t index = 0; index < fieldsCount; index++)
        instance->putField(index, globals.nilObject);
    
    return instance;
}

TObject* SmalltalkVM::newBinaryObject(TClass* klass, size_t dataSize)
{
    // All binary objects are descendants of ByteObject
    // They could not have ordinary fields, so we may use it 
    uint32_t slotSize = sizeof(TByteObject) + correctPadding(dataSize);
    
    void* objectSlot = m_memoryManager->allocate(slotSize, &m_lastGCOccured);
    if (!objectSlot)
        return globals.nilObject;
    
    if (m_lastGCOccured)
        onCollectionOccured();
    
    TObject* instance = new (objectSlot) TObject(dataSize, klass);
    
    return instance;
}

/*template<> hptr<TObjectArray> SmalltalkVM::newObject<TObjectArray>(size_t dataSize)
{
    TClass* klass = globals.arrayClass;
    return (TObjectArray*) newOrdinaryObject(klass, sizeof(TObjectArray) + dataSize * sizeof(TObject*));
}

template<> hptr<TContext> SmalltalkVM::newObject<TContext>(size_t dataSize)
{
    TClass* klass = globals.contextClass;
    return (TContext*) newOrdinaryObject(klass, sizeof(TContext));
}

template<> hptr<TBlock> SmalltalkVM::newObject<TBlock>(size_t dataSize)
{
    TClass* klass = globals.blockClass;
    return (TBlock*) newOrdinaryObject(klass, sizeof(TBlock));
} */

TMethod* SmalltalkVM::lookupMethodInCache(TSymbol* selector, TClass* klass)
{
    uint32_t hash = reinterpret_cast<uint32_t>(selector) ^ reinterpret_cast<uint32_t>(klass);
    TMethodCacheEntry& entry = m_lookupCache[hash % LOOKUP_CACHE_SIZE];
    
    if (entry.methodName == selector && entry.receiverClass == klass) {
        m_cacheHits++;
        return entry.method;
    } else {
        m_cacheMisses++;
        return 0;
    }
}

TMethod* SmalltalkVM::lookupMethod(TSymbol* selector, TClass* klass)
{
    // First of all checking the method cache
    // Frequently called methods most likely will be there
    TMethod* result = (TMethod*) lookupMethodInCache(selector, klass);
    if (result)
        return result; // We're lucky!
    
    // Well, maybe we'll be luckier next time. For now we need to do the full search.
    // Scanning through the class hierarchy from the klass up to the Object
    for (TClass* currentClass = klass; currentClass != globals.nilObject; currentClass = currentClass->parentClass) {
        TDictionary* methods = currentClass->methods;
        result = (TMethod*) methods->find(selector);
        if (result)
            return result;
    }
    
    return 0;
}

void SmalltalkVM::flushMethodCache()
{
    for (size_t i = 0; i < LOOKUP_CACHE_SIZE; i++)
        m_lookupCache[i].methodName = 0;
}

void SmalltalkVM::printByteObject(TByteObject* value) {
    std::string data((const char*) value->getBytes(), value->getSize());
    printf("'%s' ", data.c_str());
}

void SmalltalkVM::printValue(uint32_t index, TObject* value, TObject* previousValue) {
    if (isSmallInteger(value))
        printf("\t\t%.3d %d (SmallInt)\n", index, getIntegerValue(reinterpret_cast<TInteger>(value)));
    else if (value == globals.nilObject)
        printf("\t\t%.3d nil\n", index);
    else if (value == globals.trueObject)
        printf("\t\t%.3d true\n", index);
    else if (value == globals.falseObject)
        printf("\t\t%.3d false\n", index);
    else {
        std::string className = value->getClass()->name->toString();
        
        printf("\t\t%.3d ", index);
        if (className == "Symbol") {
            printByteObject((TByteObject*) value);
        } else if (className == "String") {
            printByteObject((TByteObject*) value);
        }
        
        printf("(%s)\n", className.c_str());
    }
}

void SmalltalkVM::printContents(TObjectArray& array) {
    if (isSmallInteger(&array))
        return;
    
    TObject* previousValue = 0;
    for (uint32_t i = 0; i < array.getSize(); i++) {
        printValue(i, array[i], previousValue);
        previousValue = array[i];
    }
}

void SmalltalkVM::backTraceContext(TContext* context)
{
    TContext* currentContext = context;
    for (; currentContext != globals.nilObject; currentContext = currentContext->previousContext) {
        TMethod* currentMethod  = currentContext->method;
        TByteObject&  byteCodes = *currentMethod->byteCodes;
        TObjectArray& stack     = *currentContext->stack;
        
        TObjectArray& temporaries       = *currentContext->temporaries;
        TObjectArray& arguments         = *currentContext->arguments;
        TObjectArray& instanceVariables = *(TObjectArray*) arguments[0];
        TSymbolArray& literals          = *currentMethod->literals;
        
        if (currentContext->getClass() == globals.blockClass)
            printf("Block context %p:\n", currentContext);
        else 
            printf("Context %p:\n", currentContext);
        
        printf("\tMethod: %s>>%s bytePointer %d\n", 
               currentMethod->klass->name->toString().c_str(), 
               currentMethod->name->toString().c_str(),
               context->bytePointer);
        
        if (&instanceVariables && instanceVariables.getSize()) {
            printf("\n\tInstance variables:\n");
            printContents(instanceVariables);
        }
        
        if (&arguments && arguments.getSize()) {
            printf("\n\tArguments:\n");
            printContents(arguments);
        }
        
        if (&temporaries && temporaries.getSize()) {
            printf("\n\tTemporaries:\n");
            printContents(temporaries);
        }
        
        if (&literals && literals.getSize()) {
            printf("\n\tLiterals:\n");
            printContents((TObjectArray&) literals);
        }
        
        if (&stack && stack.getSize()) {
            printf("\n\tStack (top %d):\n", getIntegerValue(context->stackTop));
            printContents(stack);
        }
        
        printf("\n\n");
    }
}

SmalltalkVM::TExecuteResult SmalltalkVM::execute(TProcess* process, uint32_t ticks)
{
    hptr<TProcess> currentProcess = newPointer(process);
    
    TVMExecutionContext ec(m_memoryManager);
    ec.push(process); // FIXME get rid of this
    ec.currentContext = process->context;
    ec.loadPointers(); // Loads bytePointer & stackTop
    
    while (true) {
        // Initializing helper references
        TByteObject&  byteCodes = * ec.currentContext->method->byteCodes;
        TObjectArray& stack     = * ec.currentContext->stack;
        
        TObjectArray& temporaries       = * ec.currentContext->temporaries;
        TObjectArray& arguments         = * ec.currentContext->arguments;
        TObjectArray& instanceVariables = * (TObjectArray*) arguments[0];
        TSymbolArray& literals          = * ec.currentContext->method->literals;
        
        if (ticks && (--ticks == 0)) {
            // Time frame expired
            ec.storePointers();
            currentProcess->context = ec.currentContext;
            currentProcess->result  = ec.returnedValue;
            
            ec.pop(); // FIXME get rid of this
//             TProcess* newProcess = (TProcess*) ec.pop(); ec.rootStack.pop_back();
//             newProcess->context = ec.currentContext;
//             newProcess->result  = ec.returnedValue;
//             ec.storePointers();
            return returnTimeExpired;
        }
            
        // decoding the instruction
        //TInstruction instruction;
        ec.instruction.low = (ec.instruction.high = byteCodes[ec.bytePointer++]) & 0x0F;
        ec.instruction.high >>= 4;
        if (ec.instruction.high == extended) {
            ec.instruction.high = ec.instruction.low;
            ec.instruction.low = byteCodes[ec.bytePointer++];
        }
        
        switch (ec.instruction.high) { // 6 pushes, 2 assignments, 1 mark, 3 sendings, 2 do's
            case pushInstance:    stack[ec.stackTop++] = instanceVariables[ec.instruction.low];     break;
            case pushArgument:    stack[ec.stackTop++] = arguments[ec.instruction.low];             break;
            case pushTemporary:   stack[ec.stackTop++] = temporaries[ec.instruction.low];           break;
            case pushLiteral:     stack[ec.stackTop++] = literals[ec.instruction.low];              break;
            case pushConstant:    doPushConstant(ec);                                               break;
            
            case pushBlock:       doPushBlock(ec);                                                  break; 
            case assignTemporary: temporaries[ec.instruction.low] = stack[ec.stackTop - 1];         break;
            case assignInstance: {
                TObject* value = stack[ec.stackTop - 1];
                instanceVariables[ec.instruction.low] = value;
                
                bool valueIsStatic = m_memoryManager->isInStaticHeap(value);
                bool slotIsStatic  = m_memoryManager->isInStaticHeap(&instanceVariables);
                
                // If adding a dynamic value to a static slot, 
                // then add this slot as a GC root
                if (slotIsStatic && !valueIsStatic) {
                    // If old value wasn't staic either, 
                    // current slot is already in the lists
                    bool oldValueIsStatic = m_memoryManager->isInStaticHeap(instanceVariables[ec.instruction.low]);
                    if (! oldValueIsStatic) {
                        m_memoryManager->removeStaticRoot(& instanceVariables[ec.instruction.low]);
                        m_memoryManager->addStaticRoot(& instanceVariables[ec.instruction.low]);
                    }
                }
                
            } break;
                
            case markArguments: doMarkArguments(ec); break; 
            case sendMessage:   doSendMessage(ec);   break;
            case sendUnary:     doSendUnary(ec);     break;
            case sendBinary:    doSendBinary(ec);    break;
                
            case doPrimitive: {
                //DoPrimitive is a hack. The interpretator never reaches opcodes after this one,
                // albeit the compiler generates opcodes. But there are expections to the rules...
                
                uint8_t primitiveNumber = byteCodes[ec.bytePointer++];
                ec.returnedValue = doExecutePrimitive(primitiveNumber, *process, ec);

                // primitiveNumber exceptions:
                // 19 - error trap            TODO
                // 8  - block invocation
                // 34 - flush method cache    TODO
                        
                switch (primitiveNumber) {
                    case blockInvoke:
                        // We do not want to leave the block context which was just loaded
                        // So we're continuing without context switching
                        break;
                        
                    default:
                        // We have executed a primitive. Now we have to reject the current 
                        // execution context and push the result onto the previous context's stack
                        ec.currentContext = ec.currentContext->previousContext;
                        
                        // Inject the result...
                        ec.stackTop = getIntegerValue(ec.currentContext->stackTop);
                        (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
                        
                        // Save the stack pointer
                        ec.currentContext->stackTop = newInteger(ec.stackTop);
                        
                        ec.bytePointer = getIntegerValue(ec.currentContext->bytePointer);
                        // TODO lastReceiver = (*currentContext->arguments)[0][0].getClass();
                }
            } break;
                
            case doSpecial: {
                TExecuteResult result = doDoSpecial(process, ec);
                if (result != returnNoReturn)
                    return result;
            } break;
            
            default:
                fprintf(stderr, "Invalid opcode %d at offset %d in method ", ec.instruction.high, ec.bytePointer);
                fprintf(stderr, "'%s' of class '%s' \n", 
                        ec.currentContext->method->name->toString().c_str(), 
                        ec.lastReceiver == globals.nilObject ? "unknown" : ec.lastReceiver->name->toString().c_str());
                ec.storePointers();
                backTraceContext(ec.currentContext);
                exit(1);
        }
    }
}


void SmalltalkVM::doPushBlock(TVMExecutionContext& ec) 
{
    hptr<TByteObject>  byteCodes = newPointer(ec.currentContext->method->byteCodes);
    hptr<TObjectArray> stack     = newPointer(ec.currentContext->stack);
        
    // Block objects are usually inlined in the wrapping method code
    // pushBlock operation creates a block object initialized
    // with the proper bytecode, stack, arguments and the wrapping context.
    
    // Blocks are not executed directly. Instead they should be invoked
    // by sending them a 'value' method. Thus, all we need to do here is initialize 
    // the block object and then skip the block body by incrementing the bytePointer
    // to the block's bytecode' size. After that bytePointer will direct to the place 
    // right after the block's body. There we'll probably find the actual invoking code
    // such as sendMessage to a receiver with our block as a parameter or something similar.
    
    // Reading new byte pointer that points to the code right after the inline block
    uint16_t newBytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
    
    // Skipping the newBytePointer's data
    ec.bytePointer += 2;
    
    // Creating block object
    hptr<TBlock> newBlock = newObject<TBlock>();
    
    // Allocating block's stack
    uint32_t stackSize = getIntegerValue(ec.currentContext->method->stackSize);
    newBlock->stack    = newObject<TObjectArray>(stackSize, false);
    
    newBlock->blockBytePointer = newInteger(ec.bytePointer);
    newBlock->bytePointer      = 0;
    newBlock->stackTop         = 0;
    newBlock->previousContext  = 0;
    newBlock->argumentLocation = newInteger(ec.instruction.low);
    
    // Assigning creatingContext depending on the hierarchy
    // Nested blocks inherit the outer creating context
    if (ec.currentContext->getClass() == globals.blockClass)
        newBlock->creatingContext = ec.currentContext.cast<TBlock>()->creatingContext;
    else
        newBlock->creatingContext = ec.currentContext;
    
    newBlock->method      = ec.currentContext->method;
    newBlock->arguments   = ec.currentContext->arguments;
    newBlock->temporaries = ec.currentContext->temporaries;
    
    // Setting the execution point to a place right after the inlined block,
    // leaving the block object on top of the stack:
    ec.bytePointer = newBytePointer;
    stack[ec.stackTop++] = newBlock;
}

void SmalltalkVM::doMarkArguments(TVMExecutionContext& ec) 
{
    hptr<TObjectArray> args = newObject<TObjectArray>(ec.instruction.low, false);
    TObjectArray& stack = * ec.currentContext->stack;
    
    // This operation takes instruction.low arguments 
    // from the top of the stack and creates new array with them
    
    uint32_t index = ec.instruction.low;
    while (index > 0)
        args[--index] = stack[--ec.stackTop];
    
    stack[ec.stackTop++] = args;
}

void SmalltalkVM::doSendMessage(TVMExecutionContext& ec, TSymbol* selector, TObjectArray* arguments)
{
    TObjectArray& stack = * ec.currentContext->stack;
    hptr<TObjectArray> messageArguments = newPointer(arguments);
    
    TObject* receiver      = (* arguments)[0];
    TClass*  receiverClass = isSmallInteger(receiver) ? globals.smallIntClass : receiver->getClass();
    
    hptr<TMethod> receiverMethod = newPointer(lookupMethod(selector, receiverClass));
    
    if (receiverMethod == 0) {
        fprintf(stderr, "Failed to lookup selector '%s' of class '%s' ", 
                selector->toString().c_str(), receiverClass->name->toString().c_str());
        fprintf(stderr, "at offset %d in method '%s' \n", 
                ec.bytePointer - 1, ec.currentContext->method->name->toString().c_str());
        
        ec.currentContext->bytePointer = newInteger(ec.bytePointer);
        ec.currentContext->stackTop = newInteger(ec.stackTop);
        backTraceContext(ec.currentContext);
        
        exit(1);
    }
    
    // Save stack and opcode pointers
    ec.storePointers();
    
    // Create a new context from the giving method and arguments
    hptr<TContext> newContext   = newObject<TContext>();
    
    newContext->arguments       = arguments;
    newContext->method          = receiverMethod;
    newContext->previousContext = ec.currentContext;
    newContext->stack           = newObject<TObjectArray>(getIntegerValue(receiverMethod->stackSize), false);
    newContext->temporaries     = newObject<TObjectArray>(getIntegerValue(receiverMethod->temporarySize), false);
    newContext->stackTop        = newInteger(0);
    newContext->bytePointer     = newInteger(0);
    
    // Replace current context with the new one
    ec.currentContext = newContext;
    ec.loadPointers();
    ec.lastReceiver   = newPointer(receiverClass);
}

void SmalltalkVM::doSendMessage(TVMExecutionContext& ec)
{
    TObjectArray& stack = * ec.currentContext->stack;
    TObjectArray* messageArguments = (TObjectArray*) stack[--ec.stackTop];
    
    // These do not need to be hptr'ed
    TSymbolArray& literals   =  * ec.currentContext->method->literals;
    TSymbol* messageSelector = literals[ec.instruction.low];
    
    doSendMessage(ec, messageSelector, messageArguments);
}

void SmalltalkVM::doSendUnary(TVMExecutionContext& ec)
{ 
    TObjectArray& stack = *ec.currentContext->stack;
    
    // isNil notNil //TODO in the future: catch instruction.low != 0 or 1
    
    TObject* top = stack[--ec.stackTop];
    bool result  = (top == globals.nilObject);

    if (ec.instruction.low != 0)
        result = not result;

    ec.returnedValue = result ? globals.trueObject : globals.falseObject;
    stack[ec.stackTop++] = ec.returnedValue;
}

void SmalltalkVM::doSendBinary(TVMExecutionContext& ec)
{
    hptr<TObjectArray> stack  = newPointer(ec.currentContext->stack);
    
    // Loading operand objects
    hptr<TObject> rightObject = newPointer(stack[--ec.stackTop]);
    hptr<TObject> leftObject  = newPointer(stack[--ec.stackTop]);
    
    // If operands are both small integers, we need to handle it ourselves
    if (isSmallInteger(leftObject) && isSmallInteger(rightObject)) {
        // Loading actual operand values
        uint32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject.rawptr()));
        uint32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject.rawptr()));
        
        // Performing an operation
        switch (ec.instruction.low) {
            case 0: // operator <
                ec.returnedValue = (leftOperand < rightOperand) ? globals.trueObject : globals.falseObject;
                break;
            
            case 1: // operator <=
                ec.returnedValue = (leftOperand <= rightOperand) ? globals.trueObject : globals.falseObject;
                break;
            
            case 2: // operator +
                //FIXME possible overflow?
                ec.returnedValue = reinterpret_cast<TObject*>(newInteger(leftOperand+rightOperand)); 
                break;
        }
        
        // Pushing result back to the stack
        stack[ec.stackTop++] = ec.returnedValue;
    } else {
        // This binary operator is performed on an ordinary object.
        // We do not know how to handle it, thus send the message to the receiver
        
        TObjectArray* messageArguments = newObject<TObjectArray>(2, false);
        (*messageArguments)[1] = rightObject;
        (*messageArguments)[0] = leftObject;
        
        TSymbol* messageSelector = (TSymbol*) globals.binaryMessages[ec.instruction.low];
        doSendMessage(ec, messageSelector, messageArguments);
    }
} 

SmalltalkVM::TExecuteResult SmalltalkVM::doDoSpecial(TProcess*& process, TVMExecutionContext& ec)
{
    TByteObject&  byteCodes         = * ec.currentContext->method->byteCodes;
    TObjectArray& stack             = * ec.currentContext->stack;
    TObjectArray& temporaries       = * ec.currentContext->temporaries;
    TObjectArray& arguments         = * ec.currentContext->arguments;
    TObjectArray& instanceVariables = *(TObjectArray*) arguments[0];
    TSymbolArray& literals          = * ec.currentContext->method->literals;
    
    switch(ec.instruction.low) {
        case selfReturn: {
            ec.returnedValue = arguments[0]; // self
            ec.currentContext = ec.currentContext->previousContext;
            ec.loadPointers();
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
        } break;
        
        case stackReturn:
        {
            ec.returnedValue  = stack[--ec.stackTop];
            ec.currentContext = ec.currentContext->previousContext;
            ec.loadPointers();
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
        } break;
        
        case blockReturn: {
            ec.returnedValue = stack[--ec.stackTop];
            TBlock* contextAsBlock = ec.currentContext.cast<TBlock>();
            ec.currentContext = contextAsBlock->creatingContext->previousContext;
            ec.loadPointers();
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
        } break;
                        
        case duplicate: {
            // Duplicate an object on the stack
            TObject* copy = stack[ec.stackTop - 1];
            stack[ec.stackTop++] = copy;
        } break;
        
        case popTop: ec.stackTop--; break;
        case branch: 
            ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            break;
        
        case branchIfTrue: {
            ec.returnedValue = stack[--ec.stackTop];
            
            if(ec.returnedValue == globals.trueObject)
                ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            else
                ec.bytePointer += 2;
        } break;
        
        case branchIfFalse: {
            ec.returnedValue = stack[--ec.stackTop];
            
            if(ec.returnedValue == globals.falseObject)
                ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            else
                ec.bytePointer += 2;
        } break;
        
        case sendToSuper: {
            uint32_t literalIndex    = byteCodes[ec.bytePointer++];
            TSymbol* messageSelector = literals[literalIndex];
            TClass*  receiverClass   = ec.currentContext->method->klass->parentClass;
            TObjectArray* messageArguments = (TObjectArray*) stack[--ec.stackTop];
            
            doSendMessage(ec, messageSelector, messageArguments);
        } break;
        
        case breakpoint: {
            ec.bytePointer -= 1;
            
            // FIXME do not waste time to store process on the stack. we do not need it
            process = (TProcess*) ec.pop();
            process->context = ec.currentContext;
            process->result = ec.returnedValue;
            
            ec.storePointers();
            return returnBreak;
        } break;
        
    }
    
    return returnNoReturn;
}

void SmalltalkVM::doPushConstant(TVMExecutionContext& ec)
{
    TObjectArray& stack = *ec.currentContext->stack;
    uint8_t    constant = ec.instruction.low;
    switch (constant) {
        case 0: 
        case 1: 
        case 2: 
        case 3: 
        case 4: 
        case 5: 
        case 6: 
        case 7: 
        case 8: 
        case 9: 
            stack[ec.stackTop++] = (TObject*) newInteger(constant);
            break;
            
        case nilConst:   stack[ec.stackTop++] = globals.nilObject;   break;
        case trueConst:  stack[ec.stackTop++] = globals.trueObject;  break;
        case falseConst: stack[ec.stackTop++] = globals.falseObject; break;
        default:
            /* TODO unknown push constant */ ;
            stack[ec.stackTop++] = globals.nilObject;
            
    }
}

TObject* SmalltalkVM::doExecutePrimitive(uint8_t opcode, TProcess& process, TVMExecutionContext& ec)
{
    TObjectArray& stack = *ec.currentContext->stack;
    
    switch(opcode) {
        case returnIsEqual: { // 1
            TObject* arg2   = stack[--ec.stackTop];
            TObject* arg1   = stack[--ec.stackTop];
            
            if(arg1 == arg2)
                return globals.trueObject;
            else
                return globals.falseObject;
        } break;
        
        case returnClass: { // 2
            TObject* object = stack[--ec.stackTop];
            return isSmallInteger(object) ? globals.smallIntClass : object->getClass();
        } break;
        
        case ioPutChar: { // 3
            TInteger charObject = reinterpret_cast<TInteger>(stack[--ec.stackTop]);
            uint8_t  charValue = getIntegerValue(charObject);
            putchar(charValue);
            return globals.nilObject;
        } break;
        
        case ioGetChar: { // 9
            int32_t input = getchar();
            if(input == EOF)
                return globals.nilObject;
            else
                return reinterpret_cast<TObject*>(newInteger(input));
        } break;
        
        case returnSize: {
            TObject* object = stack[--ec.stackTop];
            uint32_t returnedSize = isSmallInteger(object) ? 0 : object->getSize();
            return reinterpret_cast<TObject*>(newInteger(returnedSize));
        } break;
        
        case 6: { // start new process
            TInteger value = reinterpret_cast<TInteger>(stack[--ec.stackTop]);
            uint32_t ticks = getIntegerValue(value);
            TProcess* newProcess = (TProcess*) stack[--ec.stackTop];
            
            // FIXME possible stack overflow due to recursive call
            int result = this->execute(newProcess, ticks);
            return reinterpret_cast<TObject*>(newInteger(result));
        } break;
        
        case allocateObject: { // 7
            TObject* size  = stack[--ec.stackTop];
            TClass*  klass = (TClass*) stack[--ec.stackTop];
            uint32_t sizeInPointers = getIntegerValue(reinterpret_cast<TInteger>(size));
            return newOrdinaryObject(klass, (sizeInPointers + 2) * sizeof(TObject*)); 
        } break;
        
        case blockInvoke: { // 8
            TBlock* block = (TBlock*) stack[--ec.stackTop];
            uint32_t argumentLocation = getIntegerValue(block->argumentLocation);
            
            // Checking the passed temps size
            TObjectArray* blockTemps = block->temporaries;
            
            // Amount of arguments stored on the stack except the block itself
            uint32_t argCount = ec.instruction.low - 1;
            
            if (argCount >  (blockTemps ? blockTemps->getSize() : 0) ) {
                ec.stackTop -= (argCount  + 1); // unrolling stack
                
                /* TODO correct primitive failing
                 * Since we're continuing execution from a failed
                 * primitive, re-fetch context if a GC had occurred
                 * during the failed execution.  Supply a return value
                 * for the failed primitive.
                 *
                //returnedValue = nilObject;
                if(context != rootStack[--rootTop])
                {
                    context = rootStack[rootTop];
                    method = context->data[methodInContext];
                    stack = context->data[stackInContext];
                    bp = bytePtr(method->data[byteCodesInMethod]);
                    arguments = temporaries = literals = instanceVariables = 0;
                } */
                stack[ec.stackTop++] = globals.nilObject;
                return globals.nilObject;
            }
                
            // Loading temporaries array
            for (uint32_t index = argCount - 1, count = argCount; count > 0; index--, count--)
                (*blockTemps)[argumentLocation + index] = stack[--ec.stackTop];
            
            // Switching execution context to the invoking block
            block->previousContext = ec.currentContext->previousContext;
            ec.currentContext = (TContext*) block;
            ec.stackTop = 0; // resetting stack
            
            // Block is bound to the method's bytecodes, so it's
            // first bytecode will not be zero but the value specified 
            ec.bytePointer = getIntegerValue(block->blockBytePointer);
            
            // Popping block object from the stack
            //ec.pop();
            return block;
        } break;
        
        case smallIntAdd:        // 10
        case smallIntDiv:        // 11
        case smallIntMod:        // 12
        case smallIntLess:       // 13
        case smallIntEqual:      // 14
        case smallIntMul:        // 15
        case smallIntSub:        // 16
        case smallIntBitOr:      // 36
        case smallIntBitAnd:     // 37
        case smallIntBitShift: { // 39
            // Loading operand objects
            TObject* rightObject = stack[--ec.stackTop];
            TObject* leftObject  = stack[--ec.stackTop];
            if ( !isSmallInteger(leftObject) || !isSmallInteger(rightObject) ) {
                failPrimitive(stack, ec.stackTop);
                break;
            }
                
            // Extracting values
            uint32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
            uint32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
            
            // Performing an operation
            TObject* result = doSmallInt((SmallIntOpcode) opcode, leftOperand, rightOperand);
            if (result == globals.nilObject) {
                failPrimitive(stack, ec.stackTop);
                break;
            }
            return result;
        } break;
        
        // TODO case 18 // turn on debugging
        
        case 19: { // error
            ec.pop();
            TContext* context = process.context;
            process = *(TProcess*) ec.pop(); 
            process.context = context;
            //return returnError; TODO cast 
        } break;
        
        case allocateByteArray: { // 20
            uint32_t objectSize = getIntegerValue(reinterpret_cast<TInteger>(stack[--ec.stackTop]));
            TClass*  klass = (TClass*) stack[--ec.stackTop];
            
            return newBinaryObject(klass, objectSize);
        } break;
        
        case arrayAt:      // 24 
        case arrayAtPut: { // 5
            TObject* indexObject = stack[--ec.stackTop];
            TObjectArray* array  = (TObjectArray*) stack[--ec.stackTop];
            TObject* valueObject;
            
            // If the method is Array:at:put then pop a value from the stack
            if (opcode == arrayAtPut) 
                valueObject = stack[--ec.stackTop];
            
            if (! isSmallInteger(indexObject) ) {
                failPrimitive(stack, ec.stackTop);
                break;
            }

            // Smalltalk indexes arrays starting from 1, not from 0
            // So we need to recalculate the actual array index before
            uint32_t actualIndex = getIntegerValue(reinterpret_cast<TInteger>(indexObject)) - 1; 
            
            // Checking boundaries
            if (actualIndex >= array->getSize()) {
                failPrimitive(stack, ec.stackTop);
                break;
            }
            
            if(opcode == arrayAt) 
                return array->getField(actualIndex);
            else { 
                // Array:at:put
                array->putField(actualIndex, valueObject);
                
                // TODO gc ?
                
                // Return self
                return (TObject*) array;
            }
        } break;
        
        case stringAt:      // 21
        case stringAtPut: { // 22
            TObject* indexObject = stack[--ec.stackTop];
            TString* string      = (TString*) stack[--ec.stackTop];
            TObject* valueObject;
            
            // If the method is String:at:put then pop a value from the stack
            if (opcode == stringAtPut) 
                valueObject = stack[--ec.stackTop];
            
            if ( !isSmallInteger(indexObject) ) {
                failPrimitive(stack, ec.stackTop);
                break;
            }
            
            // Smalltalk indexes arrays starting from 1, not from 0
            // So we need to recalculate the actual array index before
            uint32_t actualIndex = getIntegerValue(reinterpret_cast<TInteger>(indexObject)) - 1;
            
            // Checking boundaries
            if (actualIndex >= string->getSize()) {
                failPrimitive(stack, ec.stackTop);
                break;
            }
            
            if(opcode == stringAt) 
                // String:at
                return reinterpret_cast<TObject*>(newInteger( string->getByte(actualIndex) ));
            else { 
                // String:at:put
                TInteger value = reinterpret_cast<TInteger>(valueObject);
                string->putByte(actualIndex, getIntegerValue(value));
                return (TObject*) string;
            }
        } break;
        
        case cloneByteObject: { // 23
            TClass* klass = (TClass*) stack[--ec.stackTop];
            hptr<TByteObject> original = newPointer((TByteObject*) stack[--ec.stackTop]);
            
            // Creating clone
            uint32_t dataSize  = original->getSize();
            hptr<TByteObject> clone = newPointer((TByteObject*) newBinaryObject(klass, dataSize * sizeof(TObject*)));
            
            // Cloning data
            memcpy(clone->getBytes(), original->getBytes(), dataSize);
            return (TObject*) clone;
        } break;
        
//         case integerDiv:   // Integer /
//         case integerMod:   // Integer %
//         case integerAdd:   // Integer +
//         case integerMul:   // Integer *
//         case integerSub:   // Integer -
//         case integerLess:  // Integer <
//         case integerEqual: // Integer ==
//             //TODO integer operations
//             break;
        
        case integerNew: { // 32
            TObject* object = stack[--ec.stackTop];
            if (! isSmallInteger(object)) {
                failPrimitive(stack, ec.stackTop);
                break;
            }
            
            TInteger integer = reinterpret_cast<TInteger>(object);
            uint32_t value = getIntegerValue(integer);
            
            return reinterpret_cast<TObject*>(newInteger(value)); // FIXME long integer
        } break;
        
        case flushCache: // 34
            //FIXME returnedValue is not to be globals.nilObject
            flushMethodCache();
            break;
        
        case bulkReplace: { // 38
            //Implementation of replaceFrom:to:with:startingAt: as a primitive
            TObject* destination            = stack[--ec.stackTop];
            TObject* destinationStartOffset = stack[--ec.stackTop];
            TObject* destinationStopOffset  = stack[--ec.stackTop];
            TObject* source                 = stack[--ec.stackTop];
            TObject* sourceStartOffset      = stack[--ec.stackTop];
            
            bool succeeded  = doBulkReplace( destination, destinationStartOffset, 
                                             destinationStopOffset, source, 
                                             sourceStartOffset );
            
            if(!succeeded) {
                failPrimitive(stack, ec.stackTop);
                break;
            }
            return destination;
        } break;
        
        // TODO cases 33, 35, 40
        
        default: {
            hptr<TObjectArray> pStack = newPointer(ec.currentContext->stack);
            
            uint32_t argCount = ec.instruction.low;
            hptr<TObjectArray> args = newObject<TObjectArray>(argCount);
            
            uint32_t i = argCount;
            while (i > 0)
                args[--i] = pStack[--ec.stackTop];
            
            //TODO call primitive
            fprintf(stderr, "unimplemented or invalid primitive %d\n", opcode);
        }
    }
    
    return globals.nilObject;
}

TObject* SmalltalkVM::doSmallInt( SmallIntOpcode opcode, uint32_t leftOperand, uint32_t rightOperand)
{
    switch(opcode) {
        case smallIntAdd:
            return reinterpret_cast<TObject*>(newInteger( leftOperand + rightOperand )); //FIXME possible overflow
        
        case smallIntDiv:
            if (rightOperand == 0)
                return globals.nilObject;
            return reinterpret_cast<TObject*>(newInteger( leftOperand / rightOperand ));
        
        case smallIntMod:
            if (rightOperand == 0)
                return globals.nilObject;
            return reinterpret_cast<TObject*>(newInteger( leftOperand % rightOperand ));
        
        case smallIntLess:
            if (leftOperand < rightOperand)
                return globals.trueObject;
            else
                return globals.falseObject;
        
        case smallIntEqual:
            if (leftOperand == rightOperand)
                return globals.trueObject;
            else
                return globals.falseObject;
        
        case smallIntMul:
            return reinterpret_cast<TObject*>(newInteger( leftOperand * rightOperand )); //FIXME possible overflow
        
        case smallIntSub:
            return reinterpret_cast<TObject*>(newInteger( leftOperand - rightOperand )); //FIXME possible overflow
        
        case smallIntBitOr:
            return reinterpret_cast<TObject*>(newInteger( leftOperand | rightOperand ));
        
        case smallIntBitAnd:
            return reinterpret_cast<TObject*>(newInteger( leftOperand & rightOperand ));
        
        case smallIntBitShift: { 
            // operator << if rightOperand < 0, operator >> if rightOperand >= 0
            
            uint32_t result = 0;
            int32_t  signedRightOperand = (int32_t) rightOperand;
            
            if (signedRightOperand < 0) {
                //shift right 
                result = leftOperand >> -signedRightOperand;
            } else {
                // shift left ; catch overflow 
                result = leftOperand << rightOperand;
                if (leftOperand > result) {
                    return globals.nilObject;
                }
            }
            
            return reinterpret_cast<TObject*>(newInteger( result ));
        }
        
        default: 
            return globals.nilObject; /* FIXME possible error */
    }
}

//TODO replace it later with a proper implementation.
//failPrimitive should push nil into the stack(but it is a normal behaviour(doExecutePrimitive should put the result of execution into the stack)
//but we need to handle situations like error trapping etc
//we may put nil outside of doExecutePrimitive function and handle special situations by arg-ptr
void SmalltalkVM::failPrimitive(TObjectArray& stack, uint32_t& stackTop) {
    stack[stackTop++] = globals.nilObject;
}

void SmalltalkVM::onCollectionOccured()
{
    // Here we need to handle the GC collection event
    flushMethodCache();
    
    // TODO During the VM execution we may need to reload the context
}

bool SmalltalkVM::doBulkReplace( TObject* destination, TObject* destinationStartOffset, TObject* destinationStopOffset, TObject* source, TObject* sourceStartOffset) {
    
    if ( ! isSmallInteger(sourceStartOffset) || ! isSmallInteger(destinationStartOffset) || ! isSmallInteger(destinationStopOffset) )
        return false;

    // Smalltalk indexes are counted starting from 1. 
    // We need to decrement all values to get the zero based index:
    int32_t iSourceStartOffset      = getIntegerValue(reinterpret_cast<TInteger>( sourceStartOffset )) - 1; 
    int32_t iDestinationStartOffset = getIntegerValue(reinterpret_cast<TInteger>( destinationStartOffset )) - 1;
    int32_t iDestinationStopOffset  = getIntegerValue(reinterpret_cast<TInteger>( destinationStopOffset )) - 1;
    int32_t iCount                  = iDestinationStopOffset - iDestinationStartOffset + 1;
    
    if ( iSourceStartOffset < 0 || iDestinationStartOffset < 0 || iDestinationStopOffset < 0 || iCount < 1)
        return false;
    
    if (destination->getSize() < iDestinationStopOffset || source->getSize() < (iSourceStartOffset + iCount) )
        return false;
    
    if ( source->isBinary() && destination->isBinary() ) {
        uint8_t* sourceBytes      = static_cast<TByteObject*>(source)->getBytes();
        uint8_t* destinationBytes = static_cast<TByteObject*>(destination)->getBytes();
        
        memcpy( & destinationBytes[iDestinationStartOffset], & sourceBytes[iSourceStartOffset], iCount );
        return true;
    }
    
    if ( ! source->isBinary() && ! destination->isBinary() ) {
        // Interpreting pointer array as raw byte sequence
        uint8_t* sourceFields      = reinterpret_cast<uint8_t*>(source->getFields());
        uint8_t* destinationFields = reinterpret_cast<uint8_t*>(destination->getFields());
        
        memcpy( & destinationFields[iDestinationStartOffset], & sourceFields[iSourceStartOffset], iCount * sizeof(TObject*));
        return true;
    }
    
    return false;
}