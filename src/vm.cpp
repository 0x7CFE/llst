#include <vm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

TObject* SmalltalkVM::newOrdinaryObject(TClass* klass, size_t slotSize)
{
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

template<> TObjectArray* SmalltalkVM::newObject<TObjectArray>(size_t dataSize)
{
    TClass* klass = globals.arrayClass;
    return (TObjectArray*) newOrdinaryObject(klass, sizeof(TObjectArray) + dataSize * sizeof(TObject*));
}

template<> TContext* SmalltalkVM::newObject<TContext>(size_t dataSize)
{
    TClass* klass = globals.contextClass;
    return (TContext*) newOrdinaryObject(klass, sizeof(TContext));
}

template<> TBlock* SmalltalkVM::newObject<TBlock>(size_t dataSize)
{
    TClass* klass = globals.blockClass;
    return (TBlock*) newOrdinaryObject(klass, sizeof(TBlock));
}

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

#define IP_VALUE (byteCodes[bytePointer] | (byteCodes[bytePointer+1] << 8))

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
    m_rootStack.push_back(process);

    TVMExecutionContext ec;
    ec.currentContext = process->context;
    ec.loadPointers(); //load bytePointer & stackTop
    
    ec.returnedValue = globals.nilObject;
    ec.lastReceiver  = (TClass*) globals.nilObject;
    
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
            TProcess* newProcess = (TProcess*) m_rootStack.back(); m_rootStack.pop_back();
            newProcess->context = ec.currentContext;
            newProcess->result  = ec.returnedValue;
            ec.storePointers();
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
            case pushInstance:    stack[ec.stackTop++] = instanceVariables[ec.instruction.low]; break;
            case pushArgument:    stack[ec.stackTop++] = arguments[ec.instruction.low];         break;
            case pushTemporary:   stack[ec.stackTop++] = temporaries[ec.instruction.low];       break;
            case pushLiteral:     stack[ec.stackTop++] = literals[ec.instruction.low];          break;
            case pushConstant:    doPushConstant(ec.instruction.low, ec);                       break;
            
            case pushBlock:       doPushBlock(ec); break; 
            case assignTemporary: temporaries[ec.instruction.low] = stack[ec.stackTop - 1];     break;
            case assignInstance:
                instanceVariables[ec.instruction.low] = stack[ec.stackTop - 1];
                // TODO isDynamicMemory()
                break;
                
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
                        // We have executed a primitive. Now we have to reject the current context execution
                        // and push the result onto the previous context's stack
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
    TByteObject&  byteCodes = * ec.currentContext->method->byteCodes;
    TObjectArray& stack     = * ec.currentContext->stack;
        
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
    ec.bytePointer += 2; // skipping the newBytePointer's data
    
    // Creating block object
    TBlock* newBlock = newObject<TBlock>();
    
    // Allocating block's stack
    uint32_t stackSize = getIntegerValue(ec.currentContext->method->stackSize);
    newBlock->stack = newObject<TObjectArray>(stackSize);
    
    // FIXME WTF? Why not newInteger(0) ?
    newBlock->bytePointer = 0;
    newBlock->stackTop = 0;
    newBlock->previousContext =  0; // Why not nilObject?
    
    newBlock->blockBytePointer = newInteger(ec.bytePointer);
    newBlock->argumentLocation = newInteger(ec.instruction.low);
    
    // Assigning creatingContext depending on the hierarchy
    // Nested blocks inherit the outer creating context
    if (ec.currentContext->getClass() == globals.blockClass)
        newBlock->creatingContext = static_cast<TBlock*>(ec.currentContext)->creatingContext;
    else
        newBlock->creatingContext = ec.currentContext;
    
    newBlock->method = ec.currentContext->method;
    newBlock->arguments = ec.currentContext->arguments;
    newBlock->temporaries = ec.currentContext->temporaries;
    
    // Setting the execution point to a place right after the inlined block,
    // leaving the block object on top of the stack:
    ec.bytePointer = newBytePointer;
    stack[ec.stackTop++] = newBlock;
    
    // args, temps, stack and other will be reloaded automatically on the text iteration
}

void SmalltalkVM::doMarkArguments(TVMExecutionContext& ec) 
{
    TObjectArray& stack = * ec.currentContext->stack;
    
    // This operation takes instruction.low arguments 
    // from the top of the stack and creates new array with them
    
    TObjectArray* args = newObject<TObjectArray>(ec.instruction.low);
    
    uint32_t index = ec.instruction.low;
    //for (int index = instruction.low - 1; index >= 0; index--)
    while (index > 0)
        (*args)[--index] = stack[--ec.stackTop];
    
    stack[ec.stackTop++] = args;
}

void SmalltalkVM::doSendMessage(TVMExecutionContext& ec)
{
    TObjectArray& stack = *ec.currentContext->stack;
    TSymbolArray& literals = *ec.currentContext->method->literals;
    
    TSymbol*      messageSelector  = literals[ec.instruction.low];
    TObjectArray* messageArguments = (TObjectArray*) stack[--ec.stackTop];
    TObject* receiver       = (*messageArguments)[0];
    TClass*  receiverClass  = isSmallInteger(receiver) ? globals.smallIntClass : receiver->getClass();
    TMethod* receiverMethod = lookupMethod(messageSelector, receiverClass);
    
    if (receiverMethod == 0) {
        fprintf(stderr, "Failed to lookup selector '%s' of class '%s' ", 
                messageSelector->toString().c_str(), receiverClass->name->toString().c_str());
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
    TContext* newContext        = newObject<TContext>();
    newContext->arguments       = messageArguments;
    newContext->method          = receiverMethod;
    newContext->previousContext = ec.currentContext;
    newContext->stack           = newObject<TObjectArray>(getIntegerValue(receiverMethod->stackSize));
    newContext->temporaries     = newObject<TObjectArray>(getIntegerValue(receiverMethod->temporarySize));
    newContext->stackTop        = newInteger(0);
    newContext->bytePointer     = newInteger(0);
    
    // Replace current context with the new one
    ec.currentContext = newContext;
    ec.loadPointers();
    ec.lastReceiver   = receiverClass;
}

void SmalltalkVM::doSendUnary(TVMExecutionContext& ec)
{ 
    TObjectArray& stack = *ec.currentContext->stack;
    
    // isNil notNil //TODO in the future: catch instruction.low != 0 or 1
    
    TObject* top = stack[--ec.stackTop];
    bool result = (top == globals.nilObject);

    if (ec.instruction.low != 0)
        result = not result;

    ec.returnedValue = result ? globals.trueObject : globals.falseObject;
    stack[ec.stackTop++] = ec.returnedValue;
}

void SmalltalkVM::doSendBinary(TVMExecutionContext& ec)
{
    TObjectArray& stack = *ec.currentContext->stack;
    
    // Loading operand objects
    TObject* rightObject = stack[--ec.stackTop];
    TObject* leftObject = stack[--ec.stackTop];
    
    // If operands are both small integers, we need to handle it ourselves
    if (isSmallInteger(leftObject) && isSmallInteger(rightObject)) {
        // Loading actual operand values
        uint32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
        uint32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
        
        // Performing an operation
        switch (ec.instruction.low) {
            case 0: // operator <
                ec.returnedValue = (leftOperand < rightOperand) ? globals.trueObject : globals.falseObject;
                break;
            
            case 1: // operator <=
                ec.returnedValue = (leftOperand <= rightOperand) ? globals.trueObject : globals.falseObject;
                break;
            
            case 2: // operator +
                ec.returnedValue = reinterpret_cast<TObject*>(newInteger(leftOperand+rightOperand)); //FIXME possible overflow?
                break;
        }
        
        // Pushing result back to the stack
        stack[ec.stackTop++] = ec.returnedValue;
    } else {
        // This binary operator is performed on an ordinary object.
        // We do not know how to handle it, so sending the operation to the receiver
        
        TObjectArray* args = newObject<TObjectArray>(2);
        (*args)[1] = rightObject;
        (*args)[0] = leftObject;
        //TODO do the call
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
            ec.returnedValue = arguments[0]; // FIXME why instanceVariables? bug?
                                          // Have a look at interp.c: 605 and 1434
            ec.currentContext = ec.currentContext->previousContext;
            ec.loadPointers();
            
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
            ec.currentContext->stackTop = newInteger(ec.stackTop);
        } break;
        
        case stackReturn:
        {
            ec.returnedValue = stack[--ec.stackTop];
            ec.currentContext = ec.currentContext->previousContext;
            ec.loadPointers();
            
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
            ec.currentContext->stackTop = newInteger(ec.stackTop);

        } break;
        
        case blockReturn: {
            ec.returnedValue = stack[--ec.stackTop];
            TBlock* contextAsBlock = (TBlock*) ec.currentContext;
            ec.currentContext = contextAsBlock->creatingContext->previousContext;
            //initVariablesFromContext(ec.currentContext, *method, byteCodes, ec.bytePointer, stack, ec.stackTop, temporaries, arguments, instanceVariables, literals);
            stack[ec.stackTop++] = ec.returnedValue;
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
            ec.instruction.low = byteCodes[ec.bytePointer++];
            TSymbol* messageSelector = literals[ec.instruction.low];
            TClass*  receiverClass   = instanceVariables.getClass();
            TMethod* method          = lookupMethod(messageSelector, receiverClass);
            //TODO do the call
        } break;
        
        case breakpoint: {
            ec.bytePointer -= 1;
            
            process = (TProcess*) m_rootStack.back(); m_rootStack.pop_back();
            process->context = ec.currentContext;
            process->result = ec.returnedValue;
            
            ec.storePointers();
            return returnBreak;
        } break;
        
    }
    
    return returnNoReturn;
}

void SmalltalkVM::doPushConstant(uint8_t constant, TVMExecutionContext& ec)
{
    TObjectArray& stack = *ec.currentContext->stack;
    
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
        case returnIsEqual: {
            TObject* arg2   = stack[--ec.stackTop];
            TObject* arg1   = stack[--ec.stackTop];
            
            if(arg1 == arg2)
                return globals.trueObject;
            else
                return globals.falseObject;
        } break;
        
        case returnClass: {
            TObject* object = stack[--ec.stackTop];
            return isSmallInteger(object) ? globals.smallIntClass : object->getClass();
        } break;
        
        case ioPutChar: {
            TInteger charObject = reinterpret_cast<TInteger>(stack[--ec.stackTop]);
            uint8_t  charValue = getIntegerValue(charObject);
            putchar(charValue);
            return globals.nilObject;
        } break;
        
        case ioGetChar: {
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
        
        case allocateObject: {
            TObject* size  = stack[--ec.stackTop];
            TClass*  klass = (TClass*) stack[--ec.stackTop];
            uint32_t sizeInPointers = getIntegerValue(reinterpret_cast<TInteger>(size));
            return newOrdinaryObject(klass, (sizeInPointers + 2) * sizeof(TObject*)); 
        } break;
        
        case blockInvoke: { 
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

//             uint32_t index = argCount;
//             while (index > 0) {
//                 (*blockTemps)[argumentLocation + index] = stack[--stackTop];
//                 index--;
//             }

            // Switching execution context to the invoking block
            block->previousContext = ec.currentContext->previousContext;
            ec.currentContext = block;
            ec.stackTop = 0; // resetting stack
            
            // Block is bound to the method's bytecodes, so it's
            // first bytecode will not be zero but the value specified 
            ec.bytePointer = getIntegerValue(block->blockBytePointer);
            
            // Popping block object from the stack
            m_rootStack.pop_back();
            return block;
        } break;
        
        case smallIntAdd:
        case smallIntDiv:
        case smallIntMod:
        case smallIntLess:
        case smallIntEqual:
        case smallIntMul:
        case smallIntSub:
        case smallIntBitOr:
        case smallIntBitAnd:
        case smallIntBitShift: {
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
            m_rootStack.pop_back();
            TContext* context = process.context;
            process = *(TProcess*) m_rootStack.back(); m_rootStack.pop_back();
            process.context = context;
            //return returnError; TODO cast 
        } break;
        
        case allocateByteArray: {
            uint32_t objectSize = getIntegerValue(reinterpret_cast<TInteger>(stack[--ec.stackTop]));
            TClass*  klass = (TClass*) stack[--ec.stackTop];
            
            size_t slotSize  = sizeof(TByteArray) + objectSize * sizeof(TByteArray*);
            void* objectSlot = m_memoryManager->allocate(slotSize);
            if (!objectSlot)
                return globals.nilObject;
            
            TByteArray* instance = (TByteArray*) new (objectSlot) TByteObject(objectSize, klass);
            return (TObject*) instance;
        } break;
        
        case arrayAt:
        case arrayAtPut: {
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
        
        case stringAt:
        case stringAtPut: {
            TObject* indexObject        = stack[--ec.stackTop];
            TString* string             = (TString*) stack[--ec.stackTop];
            TObject* valueObject;
            
            // If the method is String:at:put then pop a value from the stack
            if (opcode == 22) 
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
            
            if(opcode == 21) 
                // String:at
                return reinterpret_cast<TObject*>(newInteger( string->getByte(actualIndex) ));
            else { 
                // String:at:put
                TInteger value = reinterpret_cast<TInteger>(valueObject);
                string->putByte(actualIndex, getIntegerValue(value));
                return (TObject*) string;
            }
        } break;
        
        case cloneByteObject: {
            TClass* klass = (TClass*) stack[--ec.stackTop];
            TByteObject* original = (TByteObject*) stack[--ec.stackTop];
            
            // Creating clone
            uint32_t dataSize  = original->getSize();
            TByteObject* clone = (TByteObject*) newBinaryObject(klass, dataSize * sizeof(TObject*));
            
            // Cloning data
            for (uint32_t i = 0; i < dataSize; i++)
                (*clone)[i] = (*original)[i];
            
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
        
        case 32: { // Integer new
            TObject* object = stack[--ec.stackTop];
            if (! isSmallInteger(object)) {
                failPrimitive(stack, ec.stackTop);
                break;
            }
            
            TInteger integer = reinterpret_cast<TInteger>(object);
            uint32_t value = getIntegerValue(integer);
            
            return reinterpret_cast<TObject*>(newInteger(value)); // FIXME long integer
        } break;
        
        // TODO case 33:
        
        case 34:
            //FIXME returnedValue is not to be globals.nilObject
            flushMethodCache();
            break;
        
        // TODO cases 35, 38, 40
            
        default:
            fprintf(stderr, "unimplemented or invalid primitive %d ", opcode);
            break;
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
