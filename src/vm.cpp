#include <vm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

TObject* SmalltalkVM::newOrdinaryObject(TClass* klass, size_t slotSize)
{
    bool gcOccured = false;
    void* objectSlot = m_memoryManager->allocate(slotSize, &gcOccured);
    if (!objectSlot)
        return globals.nilObject;
    
    if (gcOccured)
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
    
    bool gcOccured = false;
    void* objectSlot = m_memoryManager->allocate(slotSize, &gcOccured);
    if (!objectSlot)
        return globals.nilObject;
    
    if (gcOccured)
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

SmalltalkVM::TExecuteResult SmalltalkVM::execute(TProcess* process, uint32_t ticks)
{
    m_rootStack.push_back(process);
    
    // current execution context and the executing method
    TContext* currentContext = process->context;
    TMethod*  currentMethod  = currentContext->method;
    
    uint32_t  bytePointer = getIntegerValue(currentContext->bytePointer);
    uint32_t    stackTop  = getIntegerValue(currentContext->stackTop);

    //initVariablesFromContext(context, *method, byteCodes, bytePointer, stack, stackTop, temporaries, arguments, instanceVariables, literals);
    TObject* returnedValue = globals.nilObject;
    TClass* lastReceiver = (TClass*) globals.nilObject;
    
    while (true) {
        TByteObject&  byteCodes = *currentMethod->byteCodes;
        TObjectArray& stack     = *currentContext->stack;
        
        TObjectArray& temporaries       = *currentContext->temporaries;
        TObjectArray& arguments         = *currentContext->arguments;
        TObjectArray& instanceVariables = *(TObjectArray*) arguments[0];
        TSymbolArray& literals          = *currentMethod->literals;
        
        if (ticks && (--ticks == 0)) {
            // Time frame expired
            TProcess* newProcess = (TProcess*) m_rootStack.back(); m_rootStack.pop_back();
            newProcess->context = currentContext;
            newProcess->result = returnedValue;
            currentContext->bytePointer = newInteger(bytePointer);
            currentContext->stackTop = newInteger(stackTop);
            return returnTimeExpired;
        }
            
        // decoding the instruction
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == extended) {
            instruction.high = instruction.low;
            instruction.low = byteCodes[bytePointer++];
        }
        
        switch (instruction.high) { // 6 pushes, 2 assignments, 1 mark, 3 sendings, 2 do's
            case pushInstance:    stack[stackTop++] = instanceVariables[instruction.low]; break;
            case pushArgument:    stack[stackTop++] = arguments[instruction.low];         break;
            case pushTemporary:   stack[stackTop++] = temporaries[instruction.low];       break;
            case pushLiteral:     stack[stackTop++] = literals[instruction.low];          break;
            case pushConstant:    doPushConstant(instruction.low, stack, stackTop);       break;
            
            case pushBlock: {
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
                uint16_t newBytePointer = byteCodes[bytePointer] | (byteCodes[bytePointer+1] << 8);
                bytePointer += 2; // skipping the newBytePointer's data
                
                // Creating block object
                TBlock* newBlock = newObject<TBlock>();
                
                // Allocating block's stack
                uint32_t stackSize = getIntegerValue(currentMethod->stackSize);
                newBlock->stack = newObject<TObjectArray>(stackSize);
                
                // FIXME WTF? Why not newInteger(0) ?
                newBlock->bytePointer = 0;
                newBlock->stackTop = 0;
                newBlock->previousContext =  0; // Why not nilObject?
                
                newBlock->blockBytePointer = newInteger(bytePointer);
                newBlock->argumentLocation = newInteger(instruction.low);
                
                // Assigning creatingContext depending on the hierarchy
                // Nested blocks inherit the outer creating context
                if (currentContext->getClass() == globals.blockClass)
                    newBlock->creatingContext = static_cast<TBlock*>(currentContext)->creatingContext;
                else
                    newBlock->creatingContext = currentContext;
                
                newBlock->method = currentContext->method;
                newBlock->arguments = currentContext->arguments;
                newBlock->temporaries = currentContext->temporaries;
                
                // Setting the execution point to a place right after the inlined block,
                // leaving the block object on top of the stack:
                bytePointer = newBytePointer;
                stack[stackTop++] = newBlock;
                
                // args, temps, stack and other will be reloaded automatically on the text iteration
            } break;
                
            case assignTemporary: temporaries[instruction.low] = stack[stackTop - 1];     break;
            case assignInstance:
                instanceVariables[instruction.low] = stack[stackTop - 1];
                // TODO isDynamicMemory()
                break;
                
            case markArguments: {
                // This operation takes instruction.low arguments 
                // from the top of the stack and creates new array with them
                
                TObjectArray* args = newObject<TObjectArray>(instruction.low);
                
                uint32_t index = instruction.low;
                //for (int index = instruction.low - 1; index >= 0; index--)
                while (index > 0)
                    (*args)[--index] = stack[--stackTop];
                
                stack[stackTop++] = args;
            } break;
                
            case sendMessage: {
                TSymbol*      messageSelector  = literals[instruction.low];
                TObjectArray* messageArguments = (TObjectArray*) stack[--stackTop];
                
                TObject* receiver       = (*messageArguments)[0];
                TClass*  receiverClass  = isSmallInteger(receiver) ? globals.smallIntClass : receiver->getClass();
                TMethod* receiverMethod = lookupMethod(messageSelector, receiverClass);
                
                if (receiverMethod == 0) {
                    fprintf(stderr, "Failed to lookup selector '%s' of class '%s' ", 
                            messageSelector->toString().c_str(), receiverClass->name->toString().c_str());
                    fprintf(stderr, "at offset %d in method '%s' \n", 
                            bytePointer - 1, currentMethod->name->toString().c_str());
                    exit(1);
                }
                
                // Save stack and opcode pointers
                currentContext->bytePointer = newInteger(bytePointer);
                currentContext->stackTop    = newInteger(stackTop);
                
                // Create a new context from the giving method and arguments
                TContext* newContext        = newObject<TContext>();
                newContext->arguments       = messageArguments;
                newContext->method          = receiverMethod;
                newContext->previousContext = currentContext;
                newContext->stack           = newObject<TObjectArray>(getIntegerValue(receiverMethod->stackSize));
                newContext->temporaries     = newObject<TObjectArray>(getIntegerValue(receiverMethod->temporarySize));
                newContext->stackTop        = newInteger(0);
                newContext->bytePointer     = newInteger(0);
                
                // Replace current context with the new one
                currentContext = newContext;
                currentMethod  = newContext->method;
                bytePointer    = getIntegerValue(newContext->bytePointer);
                stackTop       = getIntegerValue(newContext->stackTop);
                lastReceiver   = receiverClass;
            } break;
            
            case sendUnary: { // isNil notNil //TODO in the future: catch instruction.low != 0 or 1
                TObject* top = stack[--stackTop];
                bool result = (top == globals.nilObject);
                
                if (instruction.low != 0)
                    result = not result;
                
                returnedValue = result ? globals.trueObject : globals.falseObject;
                stack[stackTop++] = returnedValue;
            } break;
            
            case sendBinary: {
                // Sending a binary operator to an object
                
                // Loading operand objects
                TObject* rightObject = stack[--stackTop];
                TObject* leftObject = stack[--stackTop];
                
                // If operands are both small integers, we need to handle it ourselves
                if (isSmallInteger(leftObject) && isSmallInteger(rightObject)) {
                    // Loading actual operand values
                    uint32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
                    uint32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
                    
                    // Performing an operation
                    switch (instruction.low) {
                        case 0: // operator <
                            returnedValue = (leftOperand < rightOperand) ? globals.trueObject : globals.falseObject;
                            break;
                        
                        case 1: // operator <=
                            returnedValue = (leftOperand <= rightOperand) ? globals.trueObject : globals.falseObject;
                            break;
                        
                        case 2: // operator +
                            returnedValue = reinterpret_cast<TObject*>(newInteger(leftOperand+rightOperand)); //FIXME possible overflow?
                            break;
                    }
                    
                    // Pushing result back to the stack
                    stack[stackTop++] = returnedValue;
                } else {
                    // This binary operator is performed on an ordinary object.
                    // We do not know how to handle it, so sending the operation to the receiver
                    
                    TObjectArray* args = newObject<TObjectArray>(2);
                    (*args)[1] = rightObject;
                    (*args)[0] = leftObject;
                    //TODO do the call
                }
            } break;
                
            case doPrimitive: {
                //DoPrimitive is a hack. The interpretator never reaches opcodes after this one,
                // albeit the compiler generates opcodes. But there are expections to the rules...
                
                uint8_t primitiveNumber = byteCodes[bytePointer++];
                
                returnedValue = doExecutePrimitive(
                    primitiveNumber, instruction.low, 
                    currentContext, currentMethod, 
                    stack, stackTop, bytePointer, *process);

                // primitiveNumber exceptions:
                // 19 - error trap            TODO
                // 8  - block invocation
                // 34 - flush method cache    TODO
                        
                switch (primitiveNumber) {
                    case blockInvoke:
                        break;
                        
                    default:
                        // We have executed a primitive. Now we have to reject the current context execution
                        // and push the result onto the previous context's stack
                        currentContext = currentContext->previousContext;
                        currentMethod  = currentContext->method; // We will get byteCodes from the method in the next iteration
                        
                        // Inject the result...
                        stackTop = getIntegerValue(currentContext->stackTop);
                        (*currentContext->stack)[stackTop++] = returnedValue;
                        
                        // Save the stack pointer
                        currentContext->stackTop = newInteger(stackTop);
                        
                        bytePointer = getIntegerValue(currentContext->bytePointer);
                        // TODO lastReceiver = (*currentContext->arguments)[0][0].getClass();
                }
            } break;
                
            case doSpecial: {
                TExecuteResult result = doDoSpecial(
                    instruction, 
                    currentContext, 
                    stackTop, 
                    currentMethod, 
                    bytePointer, 
                    process, 
                    returnedValue);
                
                if (result != returnNoReturn)
                    return result;
            } break;
            
            default:
                fprintf(stderr, "Invalid opcode %d at offset %d in method ", instruction.high, bytePointer);
                fprintf(stderr, "'%s' of class '%s' \n", 
                        currentMethod->name->toString().c_str(), 
                        lastReceiver == globals.nilObject ? "unknown" : lastReceiver->name->toString().c_str());
                exit(1);
        }
    }
}


SmalltalkVM::TExecuteResult SmalltalkVM::doDoSpecial(
    TInstruction instruction, 
    TContext* context, 
    uint32_t& stackTop,
    TMethod*& method,
    uint32_t& bytePointer,
    TProcess*& process,
    TObject*& returnedValue)
{
    TByteObject&  byteCodes         = *method->byteCodes;
    TObjectArray& stack             = *context->stack;
    TObjectArray& temporaries       = *context->temporaries;
    TObjectArray& arguments         = *context->arguments;
    TObjectArray& instanceVariables = *(TObjectArray*) arguments[0];
    TSymbolArray& literals          = *method->literals;
    
    switch(instruction.low) {
        case selfReturn: {
            returnedValue = arguments[0]; // FIXME why instanceVariables? bug?
                                          // Have a look at interp.c: 605 and 1434
            context = context->previousContext;
            //initVariablesFromContext(context, *method, byteCodes, bytePointer, stack, stackTop, temporaries, arguments, instanceVariables, literals);
            stack[stackTop++] = returnedValue;
        } break;
        
        case stackReturn:
        {
            returnedValue = stack[--stackTop];
            context = context->previousContext;
            //initVariablesFromContext(context, *method, byteCodes, bytePointer, stack, stackTop, temporaries, arguments, instanceVariables, literals);
            stack[stackTop++] = returnedValue;
            

        } break;
        
        case blockReturn: {
            returnedValue = stack[--stackTop];
            TBlock* contextAsBlock = (TBlock*) context;
            context = contextAsBlock->creatingContext->previousContext;
            //initVariablesFromContext(context, *method, byteCodes, bytePointer, stack, stackTop, temporaries, arguments, instanceVariables, literals);
            stack[stackTop++] = returnedValue;
        } break;
                        
        case duplicate: {
            // Duplicate an object on the stack
            TObject* copy = stack[stackTop - 1];
            stack[stackTop++] = copy;
        } break;
        
        case popTop: stackTop--; break;
        case branch: bytePointer = IP_VALUE; break;
        
        case branchIfTrue: {
            returnedValue = stack[--stackTop];
            
            if(returnedValue == globals.trueObject)
                bytePointer = IP_VALUE;
            else
                bytePointer += 2;
        } break;
        
        case branchIfFalse: {
            returnedValue = stack[--stackTop];
            
            if(returnedValue == globals.falseObject)
                bytePointer = IP_VALUE;
            else
                bytePointer += 2;
        } break;
        
        case sendToSuper: {
            instruction.low = byteCodes[bytePointer++];
            TSymbol* messageSelector = literals[instruction.low];
            TClass*  receiverClass   = instanceVariables.getClass();
            TMethod* method          = lookupMethod(messageSelector, receiverClass);
            //TODO do the call
        } break;
        
        case breakpoint: {
            bytePointer -= 1;
            
            process = (TProcess*) m_rootStack.back(); m_rootStack.pop_back();
            process->context = context;
            process->result = returnedValue;
            context->bytePointer = getIntegerValue(bytePointer);
            context->stackTop = getIntegerValue(stackTop);
            return returnBreak;
        } break;
        
    }
    
    return returnNoReturn;
}

void SmalltalkVM::doPushConstant(uint8_t constant, TObjectArray& stack, uint32_t& stackTop)
{
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
            stack[stackTop++] = (TObject*) newInteger(constant);
            break;
            
        case nilConst:   stack[stackTop++] = globals.nilObject;   break;
        case trueConst:  stack[stackTop++] = globals.trueObject;  break;
        case falseConst: stack[stackTop++] = globals.falseObject; break;
        default:
            /* TODO unknown push constant */ ;
            stack[stackTop++] = globals.nilObject;
            
    }
}

void SmalltalkVM::doSendMessage(TSymbol* selector, TObjectArray& arguments, TContext* context, uint32_t& stackTop)
{
    TObject* receiver      = arguments[0];
    TClass*  receiverClass = isSmallInteger(receiver) ? globals.smallIntClass : receiver->getClass();
    
    TMethod* method = lookupMethod(selector, receiverClass);
    if (! method) {
        // Oops. Nothing was found.
        // Seems that current object does not understand this message
        
        if (selector == globals.badMethodSymbol) {
            // Something really bad happened
            // TODO error
            exit(1);
        }
    }
    
}

TObject* SmalltalkVM::doExecutePrimitive(
    uint8_t opcode, uint8_t loArgument, TContext*& currentContext, TMethod*& currentMethod, 
    TObjectArray& stack, uint32_t& stackTop, uint32_t& bytePointer, TProcess& process)
{
    switch(opcode) {
        case returnIsEqual: {
            TObject* arg2   = stack[--stackTop];
            TObject* arg1   = stack[--stackTop];
            
            if(arg1 == arg2)
                return globals.trueObject;
            else
                return globals.falseObject;
        } break;
        
        case returnClass: {
            TObject* object = stack[--stackTop];
            return isSmallInteger(object) ? globals.smallIntClass : object->getClass();
        } break;
        
        case ioPutChar: {
            TInteger charObject = reinterpret_cast<TInteger>(stack[--stackTop]);
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
            TObject* object = stack[--stackTop];
            uint32_t returnedSize = isSmallInteger(object) ? 0 : object->getSize();
            return reinterpret_cast<TObject*>(newInteger(returnedSize));
        } break;
        
        case inAtPut: { // 5   in: at: put:
            
        } break;
        
        case 6: { // start new process
            TInteger value = reinterpret_cast<TInteger>(stack[--stackTop]);
            uint32_t ticks = getIntegerValue(value);
            TProcess* newProcess = (TProcess*) stack[--stackTop];
            
            // FIXME possible stack overflow due to recursive call
            int result = this->execute(newProcess, ticks);
            return reinterpret_cast<TObject*>(newInteger(result));
        } break;
        
        case allocateObject: {
            TObject* size  = stack[--stackTop];
            TClass*  klass = (TClass*) stack[--stackTop];
            uint32_t sizeInPointers = getIntegerValue(reinterpret_cast<TInteger>(size));
            return newOrdinaryObject(klass, (sizeInPointers + 2) * sizeof(TObject*)); 
        } break;
        
        case blockInvoke: { 
            TBlock* block = (TBlock*) stack[--stackTop];
            uint32_t argumentLocation = getIntegerValue(block->argumentLocation);
            
            // Checking the passed temps size
            TObjectArray* blockTemps = block->temporaries;
            
            // Amount of arguments stored on the stack except the block itself
            uint32_t argCount = loArgument - 1;
            
            if (argCount >  (blockTemps ? blockTemps->getSize() : 0) ) {
                stackTop -= (argCount  + 1); // unrolling stack
                
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
                stack[stackTop++] = globals.nilObject;
                return globals.nilObject;
            }
                
                
            // Loading temporaries array
            for (uint32_t index = argCount - 1, count = argCount; count > 0; index--, count--)
                (*blockTemps)[argumentLocation + index] = stack[--stackTop];

//             uint32_t index = argCount;
//             while (index > 0) {
//                 (*blockTemps)[argumentLocation + index] = stack[--stackTop];
//                 index--;
//             }

            // Switching execution context to the invoking block
            block->previousContext = currentContext;
            currentContext = block;
            currentMethod  = block->method;
            stackTop = 0; // resetting stack
            
            // Block is bound to the method's bytecodes, so it's
            // first bytecode will not be zero but the value specified 
            bytePointer = getIntegerValue(block->blockBytePointer);
            
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
            TObject* rightObject = stack[--stackTop];
            TObject* leftObject  = stack[--stackTop];
            if ( !isSmallInteger(leftObject) || !isSmallInteger(rightObject) ) {
                failPrimitive(stack, stackTop);
                break;
            }
                
            // Extracting values
            uint32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
            uint32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
            
            // Performing an operation
            TObject* result = doSmallInt((SmallIntOpcode) opcode, leftOperand, rightOperand);
            if (result == globals.nilObject) {
                failPrimitive(stack, stackTop);
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
            uint32_t objectSize = getIntegerValue(reinterpret_cast<TInteger>(stack[--stackTop]));
            TClass*  klass = (TClass*) stack[--stackTop];
            
            size_t slotSize  = sizeof(TByteArray) + objectSize * sizeof(TByteArray*);
            void* objectSlot = m_memoryManager->allocate(slotSize);
            if (!objectSlot)
                return globals.nilObject;
            
            TByteArray* instance = (TByteArray*) new (objectSlot) TByteObject(objectSize, klass);
            return (TObject*) instance;
        } break;
        
        case arrayAt:
        case arrayAtPut: {
            TObject* indexObject = stack[--stackTop];
            TObjectArray* array  = (TObjectArray*) stack[--stackTop];
            TObject* valueObject;
            
            // If the method is Array:at:put then pop a value from the stack
            if (opcode == arrayAtPut) 
                valueObject = stack[--stackTop];
            
            if (! isSmallInteger(indexObject) ) {
                failPrimitive(stack, stackTop);
                break;
            }

            // Smalltalk indexes arrays starting from 1, not from 0
            // So we need to recalculate the actual array index before
            uint32_t actualIndex = getIntegerValue(reinterpret_cast<TInteger>(indexObject)) - 1; 
            
            // Checking boundaries
            if (actualIndex >= array->getSize()) {
                failPrimitive(stack, stackTop);
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
            TObject* indexObject        = stack[--stackTop];
            TString* string             = (TString*) stack[--stackTop];
            TObject* valueObject;
            
            // If the method is String:at:put then pop a value from the stack
            if (opcode == 22) 
                valueObject = stack[--stackTop];
            
            if ( !isSmallInteger(indexObject) ) {
                failPrimitive(stack, stackTop);
                break;
            }
            
            // Smalltalk indexes arrays starting from 1, not from 0
            // So we need to recalculate the actual array index before
            uint32_t actualIndex = getIntegerValue(reinterpret_cast<TInteger>(indexObject)) - 1;
            
            // Checking boundaries
            if (actualIndex >= string->getSize()) {
                failPrimitive(stack, stackTop);
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
            TClass* klass = (TClass*) stack[--stackTop];
            TByteObject* original = (TByteObject*) stack[--stackTop];
            
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
            TObject* object = stack[--stackTop];
            if (! isSmallInteger(object)) {
                failPrimitive(stack, stackTop);
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
