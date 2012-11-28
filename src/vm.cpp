#include <vm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

void SmalltalkVM::flushCache()
{
    for (size_t i = 0; i < LOOKUP_CACHE_SIZE; i++)
        m_lookupCache[i].methodName = 0;
}

#define IP_VALUE (byteCodes[bytePointer] | (byteCodes[bytePointer+1] << 8))

SmalltalkVM::TExecuteResult SmalltalkVM::execute(TProcess* process, uint32_t ticks)
{
    m_rootStack.push_back(process);
    
    // current execution context and the executing method
    TContext* context = process->context;
    TMethod*  method  = context->method;
    
    TByteObject& byteCodes   = *method->byteCodes;
    uint32_t     bytePointer = getIntegerValue(context->bytePointer);
    
    TObjectArray&  stack     = *context->stack;
    uint32_t stackTop = getIntegerValue(context->stackTop);

    TObjectArray& temporaries       = *context->temporaries;
    TObjectArray& arguments         = *context->arguments;
    TObjectArray& instanceVariables = *(TObjectArray*) arguments[0];
    TSymbolArray& literals          = *method->literals;
    
    TObject* returnedValue = globals.nilObject;
    
    while (true) {
        if (ticks && (--ticks == 0)) {
            // Time frame expired
            TProcess* newProcess = (TProcess*) m_rootStack.back(); m_rootStack.pop_back();
            newProcess->context = context;
            newProcess->result = returnedValue;
            context->bytePointer = newInteger(bytePointer);
            context->stackTop = newInteger(stackTop);
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
        
        switch (instruction.high) { // 6 pushes, 2 assignes, 1 mark, 3 sendings, 2 do's
            case pushInstance:    stack[stackTop++] = instanceVariables[instruction.low]; break;
            case pushArgument:    stack[stackTop++] = arguments[instruction.low];         break;
            case pushTemporary:   stack[stackTop++] = temporaries[instruction.low];       break;
            case pushLiteral:     stack[stackTop++] = literals[instruction.low];          break;
            case pushConstant: 
                doPushConstant(instruction.low, stack, stackTop); 
                break;
            case pushBlock:
                
                break;
                
            case assignTemporary: temporaries[instruction.low] = stack[stackTop - 1];     break;
            case assignInstance:
                instanceVariables[instruction.low] = stack[stackTop - 1];
                // TODO isDynamicMemory()
                break;
                
            case markArguments: {
                // This operation takes instruction.low arguments 
                // from the top of the stack and creates new array with them
                
                m_rootStack.push_back(context);
                TObjectArray* args = newObject<TObjectArray>(instruction.low);
                
                for (int index = instruction.low - 1; index > 0; index--)
                    (*args)[index] = stack[--stackTop];
                
                stack[stackTop++] = args;
            } break;
                
            case sendMessage: {
                TSymbol* messageSelector = literals[instruction.low];
                TObjectArray* messageArguments = (TObjectArray*) stack[--stackTop];
                doSendMessage(messageSelector, *messageArguments, context, stackTop); 
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
                TObject* arg2 = stack[--stackTop],
                         arg1 = stack[--stackTop];
                if( isSmallInteger(arg1) && isSmallInteger(arg2) )
                {
                    uint32_t rhs = getIntegerValue(reinterpret_cast<TInteger*>(arg2)),
                             lhs = getIntegerValue(reinterpret_cast<TInteger*>(arg1));
                    switch(instruction.low)
                    {
                        case 0: // operator <
                            returnedValue = (lhs < rhs) ? globals.trueObject : globals.falseObject;
                        break;
                        
                        case 1: // operator <=
                            returnedValue = (lhs <= rhs) ? globals.trueObject : globals.falseObject;
                        break;
                        
                        case 2: // operator +
                            returnedValue = reinterpret_cast<TObject*>(newInteger(lhs+rhs)); //FIXME possible overflow?
                        break;
                    }
                    stack[stackTop++] = returnedValue;
                } else
                {
                    TObjectArray* args = newObject<TObjectArray>(2);
                    (*args)[1] = arg2;
                    (*args)[0] = arg1;
                    //TODO call
                }
            } break;
                
            case doPrimitive: {
                uint8_t primitiveNumber = byteCodes[bytePointer++];
                m_rootStack.push_back(context);
                returnedValue = doExecutePrimitive(primitiveNumber, stack, stackTop, *process);
                //if(returnedValue == returnError) //FIXME !!!111
                //    return returnedValue;
                context = m_rootStack.back(); m_rootStack.pop_back();
                goto doReturn; //FIXME T_T
            } break;
                
            case doSpecial: {
                TExecuteResult result = doDoSpecial(
                    instruction, 
                    context, 
                    stackTop, 
                    method, 
                    bytePointer, 
                    process, 
                    returnedValue);
                
                if (result != returnNoReturn)
                    return result;
            } break;
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
    TByteObject& byteCodes          = *method->byteCodes;
    TObjectArray&  stack            = *context->stack;
    TObjectArray& temporaries       = *context->temporaries;
    TObjectArray& arguments         = *context->arguments;
    TObjectArray& instanceVariables = *(TObjectArray*) arguments[0];
    TSymbolArray& literals          = *method->literals;
    
    switch(instruction.low) {
        case SelfReturn:
            returnedValue = arguments[0];
            goto doReturn; //FIXME T_T
            
        case StackReturn:
        {
            returnedValue = stack[--stackTop];
            
            doReturn: //FIXME T_T
            context = context->previousContext;
            goto doReturn2; //FIXME T_T
            
            doReturn2:
            if(context == 0 || context == globals.nilObject) {
                process = (TProcess*) m_rootStack.back(); m_rootStack.pop_back();
                process->context = context;
                process->result = returnedValue;
                return returnReturned;
            }
            stack       = *context->stack;
            stackTop    = getIntegerValue(context->stackTop);
            stack[stackTop++] = returnedValue;
            method      = context->method;
            byteCodes   = *method->byteCodes;
            bytePointer = getIntegerValue(context->bytePointer);
        } break;
        
        case BlockReturn: //TODO
                        break;
                        
        case Duplicate: {
            TObject* duplicate = stack[stackTop - 1];
            stack[stackTop++] = duplicate;
        } break;
        
        case PopTop: stackTop--; break;
        case Branch: bytePointer = IP_VALUE; break;
        
        case BranchIfTrue: {
            returnedValue = stack[--stackTop];
            
            if(returnedValue == globals.trueObject)
                bytePointer = IP_VALUE;
            else
                bytePointer += 2;
        } break;
        
        case BranchIfFalse: {
            returnedValue = stack[--stackTop];
            
            if(returnedValue == globals.falseObject)
                bytePointer = IP_VALUE;
            else
                bytePointer += 2;
        } break;
        
        case SendToSuper: {
            instruction.low = byteCodes[bytePointer++];
            TSymbol* l_messageSelector = literals[instruction.low];
            TClass* l_receiverClass    = instanceVariables.getClass();
            TMethod* l_method          = lookupMethod(l_messageSelector, l_receiverClass);
            //TODO call
        } break;
        
        case Breakpoint: {
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
    }
}

void SmalltalkVM::doSendMessage(TSymbol* selector, TObjectArray& arguments, TContext* context, uint32_t& stackTop)
{
    TClass*  receiverClass    = (TClass*) arguments[0];
    
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

TObject* SmalltalkVM::newObject(TSymbol* className, size_t objectSize)
{
    // TODO fast access to common classes
    TClass* klass = (TClass*) m_image->getGlobal(className);
    if (!klass)
        return globals.nilObject;
    
    // Slot size is computed depending on the object type
    size_t slotSize = 0;
//     if (T::InstancesAreBinary())    
//         slotSize = sizeof(T) + objectSize;
//     else 
        slotSize = sizeof(TObject) + objectSize * sizeof(TObject*);
    
    void* objectSlot = malloc(slotSize); // TODO llvm_gc_allocate
    if (!objectSlot)
        return globals.nilObject;
    
    TObject* instance = new (objectSlot) TObject(objectSize, klass);
    for (uint32_t i = 0; i < objectSize; i++)
        instance->putField(i, globals.nilObject);
    
    return instance;
}

TObject* SmalltalkVM::newObject(TClass* klass)
{
    uint32_t fieldsCount = getIntegerValue(klass->instanceSize);
    uint32_t slotSize = sizeof(TObject) + fieldsCount * sizeof(TObject*);
    
    void* objectSlot = malloc(slotSize); // TODO llvm_gc_allocate
    if (!objectSlot)
        return globals.nilObject;
    
    TObject* instance = new (objectSlot) TObject(slotSize, klass);
    for (uint32_t i = 0; i < fieldsCount; i++)
        instance->putField(i, globals.nilObject);
    
    return instance;
}

TObject* SmalltalkVM::doExecutePrimitive(uint8_t opcode, TObjectArray& stack, uint32_t& stackTop, TProcess& process)
{
    switch(opcode)
    {
        case 1: // operator ==
        {
            TObject* arg2   = stack[--stackTop];
            TObject* arg1   = stack[--stackTop];
            
            if(arg1 == arg2)
                return globals.trueObject;
            else
                return globals.falseObject;
        } break; // TODO remove usless breaks?
        
        case 2: // return class
        {
            TObject* top = stack[--stackTop];
            bool isSmallInt = (reinterpret_cast<uint32_t>(top) & 1);
            return isSmallInt ? globals.smallIntClass : top->getClass();
        } break;
        
        case 3:
        {
            TInteger top = reinterpret_cast<TInteger>(stack[--stackTop]);
            uint8_t  charValue = getIntegerValue(top);
            //putc(charValue, stdout);
            putchar(charValue);
            return globals.nilObject;
        } break;
        
        case 4: // return size of object
        {
            TObject* top = stack[--stackTop];
            bool isSmallInt = (reinterpret_cast<uint32_t>(top) & 1);
            uint32_t returnedSize = isSmallInt ? 0 : top->getSize();
            return reinterpret_cast<TObject*>(newInteger(returnedSize));
        } break;
        
        case 5:  // Array at put
        case 24: // Array at
        {
            TObject* arg1 = stack[--stackTop];
            TObjectArray* array = (TObjectArray*) stack[--stackTop];
            TObject* val = opcode == 5 ? stack[--stackTop] : globals.nilObject; 
            if (! isSmallInteger(arg1) ) {
                failPrimitive(stack, stackTop);
                break;
            }
            
            uint32_t idx = getIntegerValue(reinterpret_cast<TInteger>(arg1)) - 1;
            if (idx >= array->getSize()) {
                failPrimitive(stack, stackTop);
                break;
            }
            
            if(opcode == 24) // Array at
                return (*array)[idx];
            else {
                (*array)[idx] = val;
                // TODO gc ?
                return reinterpret_cast<TObject*>(array);
            }
        } break;
        
        case 6: // start new process
        {
            TInteger top = reinterpret_cast<TInteger>(stack[--stackTop]);
            uint32_t ticks = getIntegerValue(top);
            TProcess* newProcess = (TProcess*) stack[--stackTop];
            
            // FIXME possible stack overflow due to recursive call
            int result = this->execute(newProcess, ticks);
            return reinterpret_cast<TObject*>(newInteger(result));
        } break;
        
        case 7: // allocate new object
        {
            TObject* size  = stack[--stackTop];
            TClass*  klass = (TClass*) stack[--stackTop];
            uint32_t fieldsCount = getIntegerValue(reinterpret_cast<TInteger>(size));
            return newObject(klass->name, fieldsCount);
        } break;
        
        case 8:
        {
            //TODO
        } break;
        
        case 9:
        {
            int32_t input = getchar();
            if(input == EOF)
                return globals.nilObject;
            else
                return reinterpret_cast<TObject*>(newInteger(input));
        } break;
        
        case 10: // small int +
        case 11: // small int /
        case 12: // small int %
        case 13: // small int <
        case 14: // small int ==
        case 15: // small int *
        case 16: // small int -
        case 36: // bit or
        case 37: // bit and
        case 39: // bit shift
        {
            uint32_t lhs, rhs;
            {
                TObject* arg1 = stack[--stackTop];
                TObject* arg2 = stack[--stackTop];
                
                if ( !isSmallInteger(arg1) || !isSmallInteger(arg2) ) {
                    failPrimitive(stack, stackTop);
                    break;
                }
                
                rhs = getIntegerValue(reinterpret_cast<TInteger>(arg1));
                lhs = getIntegerValue(reinterpret_cast<TInteger>(arg2));
            }
            TObject* result = doSmallInt(opcode, lhs, rhs);
            if (result == globals.nilObject) {
                failPrimitive(stack, stackTop);
                break;
            }
            return result;
        } break;
        
        case 18: // turn on debugging
        {
            //TODO
        } break;
        
        case 19: // error
        {
            m_rootStack.pop_back();
            TContext* context = process.context;
            process = *(TProcess*) m_rootStack.back(); m_rootStack.pop_back();
            process.context = context;
            //return returnError; TODO cast 
        } break;
        
        case 20: // ByteArray alloc
        {
            uint32_t objectSize = getIntegerValue(reinterpret_cast<TInteger>(stack[--stackTop]));
            TClass* klass = (TClass*) stack[--stackTop];
            size_t slotSize = sizeof(TByteArray) + objectSize * sizeof(TByteArray*);
            void* objectSlot = m_memoryManager->allocate(slotSize);
            if (!objectSlot)
                return globals.nilObject;
            TByteArray* instance = (TByteArray*) new (objectSlot) TByteObject(objectSize, klass);
            return reinterpret_cast<TObject*>(instance);
        } break;
        
        case 21: // String:at
        case 22: // String:at:put
        {
            TObject* arg1 = stack[--stackTop];
            TString* string = (TString*) stack[--stackTop];
            TObject* arg3 = opcode == 22 ? stack[--stackTop] : globals.nilObject;
            if (! isSmallInteger(arg1) ) {
                failPrimitive(stack, stackTop);
                break;
            }
            
            uint32_t idx = getIntegerValue(reinterpret_cast<TInteger>(arg1)) - 1;
            if (idx >= string->getSize()) {
                failPrimitive(stack, stackTop);
                break;
            }
            
            if(opcode == 21) // String:at
                return reinterpret_cast<TObject*>(newInteger( (*string)[idx] ));
            else {
                TInteger val = reinterpret_cast<TInteger>(arg3);
                (*string)[idx] = getIntegerValue(val);
                return reinterpret_cast<TObject*>(string);
            }
        } break;
        
        case 23: // ByteObject clone
        {
            TClass* klass       = (TClass*) stack[--stackTop];
            TByteObject* obj    = (TByteObject*) stack[--stackTop];
            uint32_t size       = obj->getSize();
            TByteObject* clone  = newObject(klass->name, size);
            uint32_t i          = size;
            while(i-- > 0)
                (*clone)[i] = (*obj)[i];
            clone->setClass(klass);
            return (TObject*) clone;
        } break;
        
        case 25: // Integer /
        case 26: // Integer %
        case 27: // Integer +
        case 28: // Integer *
        case 29: // Integer -
        case 30: // Integer <
        case 31: // Integer ==
        {
            //TODO
        } break;
        
        case 32: // Integer new
        {
            TObject* top = stack[--stackTop];
            if( !isSmallInteger(top) ) {
                failPrimitive(stack, stackTop);
                break;
            }
            TInteger integer = reinterpret_cast<TInteger>(top);
            uint32_t value = getIntegerValue(integer);
            return reinterpret_cast<TObject*>(newInteger(value));
        } break;
        
        case 33:
        {
            
        } break;
        
        case 34:
        {
            flushCache();
            //FIXME returnedValue is not to be globals.nilObject
        } break;
        
        case 35:
        {
            
        } break;
        
        case 38:
        {
            
        } break;
        
        case 40:
        {
            
        } break;
        
        default:
        {
            
        } break;
    }
    
    return globals.nilObject;
}

template<> TObjectArray* SmalltalkVM::newObject<TObjectArray>(size_t objectSize /*= 0*/)
{
    TClass* klass = globals.arrayClass;
    
    // Slot size is computed depending on the object type
    size_t slotSize = sizeof(TObjectArray) + objectSize * sizeof(TObjectArray*);
    
    void* objectSlot = m_memoryManager->allocate(slotSize);
    if (!objectSlot)
        return (TObjectArray*) globals.nilObject;
    
    TObjectArray* instance = (TObjectArray*) new (objectSlot) TObject(objectSize, klass);
    for (uint32_t i = 0; i < objectSize; i++)
        instance->putField(i, globals.nilObject);
    
    return instance;
}

TObject* SmalltalkVM::doSmallInt( uint32_t opcode, uint32_t lhs, uint32_t rhs)
{
    switch(opcode)
    {
        case 10: // small int +
        {
            return reinterpret_cast<TObject*>(newInteger(lhs + rhs)); //FIXME possible overflow
        }
        
        case 11: // small int /
        {
            if (rhs == 0)
                return globals.nilObject;
            return reinterpret_cast<TObject*>(newInteger(lhs / rhs));
        }
        
        case 12: // small int %
        {
            if (rhs == 0)
                return globals.nilObject;
            return reinterpret_cast<TObject*>(newInteger(lhs % rhs));
        }
        
        case 13: // small int <
        {
            if (lhs < rhs)
                return globals.trueObject;
            else
                return globals.falseObject;
        }
        
        case 14: // small int ==
        {
            if (lhs == rhs)
                return globals.trueObject;
            else
                return globals.falseObject;
        }
        
        case 15: // small int *
        {
            return reinterpret_cast<TObject*>(newInteger(lhs * rhs)); //FIXME possible overflow
        }
        
        case 16: // small int -
        {
            return reinterpret_cast<TObject*>(newInteger(lhs - rhs)); //FIXME possible overflow
        }
        
        case 36: // bit or
        {
            return reinterpret_cast<TObject*>(newInteger(lhs | rhs));
        }
        
        case 37: // bit and
        {
            return reinterpret_cast<TObject*>(newInteger(lhs & rhs));
        }
        
        case 39: // bit shift
        {
            uint32_t result = 0;
            int32_t signed_rhs = (int32_t) rhs;
            if (signed_rhs < 0) {
                //shift right 
                result = lhs >> -signed_rhs;
            } else {
                // shift left ; catch overflow 
                result = lhs << rhs;
                if (lhs > result) {
                    return globals.nilObject;
                }
            }
            return reinterpret_cast<TObject*>(newInteger( result ));
        }
        
        default: 
            return globals.nilObject; /* FIXME possible error */
    }
    return globals.nilObject;
}

void SmalltalkVM::failPrimitive(TObjectArray& stack, uint32_t& stackTop) {
    stack[stackTop++] = globals.nilObject;
}