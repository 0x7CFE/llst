#include <vm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

TObject* SmalltalkVM::newOrdinaryObject(TClass* klass, size_t slotSize)
{
    // Class may be moved during GC in allocation, 
    // so we need to protect the pointer
    hptr<TClass> pClass = newPointer(klass);
    
    void* objectSlot = m_memoryManager->allocate(slotSize, &m_lastGCOccured);
    if (!objectSlot) {
        fprintf(stderr, "VM: memory manager failed to allocate %d bytes\n", slotSize);
        return globals.nilObject;
    }
    
    if (m_lastGCOccured)
        onCollectionOccured();
    
    // Object size stored in the TSize field of any ordinary object contains
    // number of pointers except for the first two fields           
    size_t fieldsCount = slotSize / sizeof(TObject*) - 2;
    
    TObject* instance = new (objectSlot) TObject(fieldsCount, pClass);

    for (uint32_t index = 0; index < fieldsCount; index++)
        instance->putField(index, globals.nilObject);
    
    return instance;
}

TByteObject* SmalltalkVM::newBinaryObject(TClass* klass, size_t dataSize)
{
    // Class may be moved during GC in allocation, 
    // so we need to protect the pointer
    hptr<TClass> pClass = newPointer(klass);
    
    // All binary objects are descendants of ByteObject
    // They could not have ordinary fields, so we may use it 
    uint32_t slotSize = sizeof(TByteObject) + correctPadding(dataSize);
    
    void* objectSlot = m_memoryManager->allocate(slotSize, &m_lastGCOccured);
    if (!objectSlot) {
        fprintf(stderr, "VM: memory manager failed to allocate %d bytes\n", slotSize);
        return (TByteObject*) globals.nilObject;
    }
    
    if (m_lastGCOccured)
        onCollectionOccured();
    
    TByteObject* instance = new (objectSlot) TByteObject(dataSize, pClass);
    
    return instance;
}

template<> hptr<TObjectArray> SmalltalkVM::newObject<TObjectArray>(size_t dataSize, bool registerPointer)
{
    TClass* klass = globals.arrayClass;
    TObjectArray* instance = (TObjectArray*) newOrdinaryObject(klass, sizeof(TObjectArray) + dataSize * sizeof(TObject*));
    return hptr<TObjectArray>(instance, m_memoryManager, registerPointer);
}

template<> hptr<TSymbolArray> SmalltalkVM::newObject<TSymbolArray>(size_t dataSize, bool registerPointer)
{
    TClass* klass = globals.arrayClass;
    TSymbolArray* instance = (TSymbolArray*) newOrdinaryObject(klass, sizeof(TSymbolArray) + dataSize * sizeof(TObject*));
    return hptr<TSymbolArray>(instance, m_memoryManager, registerPointer);
}

template<> hptr<TContext> SmalltalkVM::newObject<TContext>(size_t dataSize, bool registerPointer)
{
    TClass* klass = globals.contextClass;
    TContext* instance = (TContext*) newOrdinaryObject(klass, sizeof(TContext));
    return hptr<TContext>(instance, m_memoryManager, registerPointer);
}

template<> hptr<TBlock> SmalltalkVM::newObject<TBlock>(size_t dataSize, bool registerPointer)
{
    TClass* klass = globals.blockClass;
    TBlock* instance = (TBlock*) newOrdinaryObject(klass, sizeof(TBlock));
    return hptr<TBlock>(instance, m_memoryManager, registerPointer);
}

bool SmalltalkVM::checkRoot(TObject* value, TObject* oldValue, TObject** objectSlot)
{
    // Here we need to perform some actions depending on whether the object slot and
    // the value resides. Generally, all pointers from the static heap to the dynamic one
    // should be tracked by the GC because it may be the only valid link to the object. 
    // Object may be collected otherwise.
    
    bool valueIsStatic = m_memoryManager->isInStaticHeap(value);
    bool slotIsStatic  = m_memoryManager->isInStaticHeap(objectSlot);
    
    // Only static slots are subject of our interest
    if (slotIsStatic) {
        bool oldValueIsStatic = m_memoryManager->isInStaticHeap(oldValue);
        
        if (!valueIsStatic) {
            // Adding dynamic value to a static slot. If slot previously contained
            // the dynamic value then it means that slot was already registered before.
            // In that case we do not need to register it again.
            
            if (oldValueIsStatic) {
                m_memoryManager->addStaticRoot(objectSlot);
                return true; // Root list was altered
            }
        } else {
            // Adding static value to a static slot. Typically it means assigning something 
            // like nilObject. We need to check what pointer was in the slot before (oldValue).
            // If it was dynamic, we need to remove the slot from the root list, so GC will not
            // try to collect a static value from the static heap (it's just a waste of time).
            
            if (!oldValueIsStatic) {
                m_memoryManager->removeStaticRoot(objectSlot);
                return true; // Root list was altered
            }
        }
    }
    
    // Root list was not altered
    return false;
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

void SmalltalkVM::updateMethodCache(TSymbol* selector, TClass* klass, TMethod* method)
{
    uint32_t hash = reinterpret_cast<uint32_t>(selector) ^ reinterpret_cast<uint32_t>(klass);
    TMethodCacheEntry& entry = m_lookupCache[hash % LOOKUP_CACHE_SIZE];
    
    entry.methodName = selector;
    entry.receiverClass = klass;
    entry.method = method;
}

TMethod* SmalltalkVM::lookupMethod(TSymbol* selector, TClass* klass)
{
    // First of all checking the method cache
    // Frequently called methods most likely will be there
    TMethod* method = (TMethod*) lookupMethodInCache(selector, klass);
    if (method)
        return method; // We're lucky!
    
    // Well, maybe we'll be luckier next time. For now we need to do the full search.
    // Scanning through the class hierarchy from the klass up to the Object
    for (TClass* currentClass = klass; currentClass != globals.nilObject; currentClass = currentClass->parentClass) {
        TDictionary* methods = currentClass->methods;
        method = (TMethod*) methods->find(selector);
        if (method) {
            // Storing result in cache
            updateMethodCache(selector, klass, method);
            return method;
        }
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
    else if (value) {
        std::string className = value->getClass()->name->toString();
        
        printf("\t\t%.3d ", index);
        if (className == "Symbol") {
            printByteObject((TByteObject*) value);
        } else if (className == "String") {
            printByteObject((TByteObject*) value);
        }
        
        printf("(%s)\n", className.c_str());
    } else 
        printf("0\n");
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
               currentContext->bytePointer);
        
        if (&arguments && arguments.getSize()) {
            printf("\n\tArguments:\n");
            printContents(arguments);
        }
        
        if (&temporaries && temporaries.getSize()) {
            printf("\n\tTemporaries:\n");
            printContents(temporaries);
        }
        
        if (!isSmallInteger(&instanceVariables) && (&instanceVariables) && instanceVariables.getSize()) {
            printf("\n\tInstance variables:\n");
            printContents(instanceVariables);
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

SmalltalkVM::TExecuteResult SmalltalkVM::execute(TProcess* p, uint32_t ticks)
{
    hptr<TProcess> currentProcess = newPointer(p);
    
    TVMExecutionContext ec(m_memoryManager);
    ec.currentContext = currentProcess->context;
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
        
        switch (ec.instruction.high) {
            case pushInstance:    stack[ec.stackTop++] = instanceVariables[ec.instruction.low]; break;
            case pushArgument:    stack[ec.stackTop++] = arguments[ec.instruction.low];         break;
            case pushTemporary:   stack[ec.stackTop++] = temporaries[ec.instruction.low];       break;
            case pushLiteral:     stack[ec.stackTop++] = literals[ec.instruction.low];          break;
            case pushConstant:    doPushConstant(ec);                                           break;
            
            case pushBlock:       doPushBlock(ec);                                              break; 
            case assignTemporary: temporaries[ec.instruction.low] = stack[ec.stackTop - 1];     break;
            case assignInstance: {
                TObject*  value      =   stack[ec.stackTop - 1];
                TObject*  oldValue   =   instanceVariables[ec.instruction.low];
                TObject** objectSlot = & instanceVariables[ec.instruction.low];
                
                // Checking whether we need to register current object slot in the GC
                checkRoot(value, oldValue, objectSlot);
//                 if (checkRoot(value, oldValue, objectSlot))
//                     printf("Root list altered for slot %p and value %p (old %p)\n",
//                             objectSlot, value, oldValue);
                
                // Performing an assignment
                instanceVariables[ec.instruction.low] = value;
            } break;
                
            case markArguments: doMarkArguments(ec); break; 
            case sendMessage:   doSendMessage(ec);   break;
            case sendUnary:     doSendUnary(ec);     break;
            case sendBinary:    doSendBinary(ec);    break;
                
            case doPrimitive: {
                //DoPrimitive is a hack. The interpretator never reaches opcodes after this one,
                // albeit the compiler generates opcodes. But there are expections to the rules...
                
                uint8_t primitiveNumber = byteCodes[ec.bytePointer++];
                bool failed = false;
                
                ec.returnedValue = doExecutePrimitive(primitiveNumber, currentProcess, ec, &failed);
                
                //If primitive failed during execution we should continue execution in the current method
                if (failed) {
                    stack[ec.stackTop++] = globals.nilObject;
                    break;
                }
                
                // primitiveNumber exceptions:
                // 19 - error trap
                // 8  - block invocation
                // 34 - flush method cache    TODO
                        
                switch (primitiveNumber) {
                    case 19:
                        fprintf(stderr, "VM: error trap on context %p\n", ec.currentContext.rawptr());
                        return returnError;
                    
                    //case flushCache:                        
                        //We don't need to put anything onto the stack
                    
                    case blockInvoke:
                        // We do not want to leave the block context which was just loaded
                        // So we're continuing without context switching
                        break;
                        
                    default:
                        // We have executed a primitive. Now we have to reject the current 
                        // execution context and push the result onto the previous context's stack
                        ec.currentContext = ec.currentContext->previousContext;
                        
                        if (ec.currentContext.rawptr() == 0 || ec.currentContext.rawptr() == globals.nilObject) {
                            currentProcess->context  = ec.currentContext;
                            currentProcess->result   = ec.returnedValue;
                            return returnReturned;
                        }
                        
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
                TExecuteResult result = doDoSpecial(currentProcess, ec);
                if (result != returnNoReturn)
                    return result;
            } break;
            
            default:
                fprintf(stderr, "VM: Invalid opcode %d at offset %d in method ", ec.instruction.high, ec.bytePointer);
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
    // to the block's bytecode' size. After that bytePointer will point to the place 
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
    
    newBlock->argumentLocation = newInteger(ec.instruction.low);
    newBlock->blockBytePointer = newInteger(ec.bytePointer);
    
    // TODO Describe why these are 0
    newBlock->bytePointer      = 0;
    newBlock->stackTop         = 0;
    newBlock->previousContext  = 0;
    
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
    hptr<TObjectArray> args  = newObject<TObjectArray>(ec.instruction.low);
    TObjectArray& stack = * ec.currentContext->stack;
    
    // This operation takes instruction.low arguments 
    // from the top of the stack and creates new array with them
    
    uint32_t index = ec.instruction.low;
    while (index > 0)
        args[--index] = stack[--ec.stackTop];
    
    stack[ec.stackTop++] = args;
}

void SmalltalkVM::doSendMessage(TVMExecutionContext& ec, TSymbol* selector, TObjectArray* arguments, TClass* receiverClass /*= 0*/ )
{
    hptr<TObjectArray> messageArguments = newPointer(arguments);
    
    if (!receiverClass) {
        TObject* receiver = messageArguments[0];
        receiverClass = isSmallInteger(receiver) ? globals.smallIntClass : receiver->getClass();
    }
    
    ec.lastReceiver = newPointer(receiverClass);
    hptr<TMethod> receiverMethod = newPointer(lookupMethod(selector, receiverClass));
    
    // Checking whether we found a method
    if (receiverMethod == 0) {
        // Oops. Method was not found. In this case we should 
        // send #doesNotUnderstand: message to the receiver

        // Looking up the #doesNotUnderstand: method:
        receiverMethod = newPointer(lookupMethod(globals.badMethodSymbol, receiverClass));
        if (receiverMethod == 0) {
            // Something goes really wrong. 
            // We could not continue the execution
            fprintf(stderr, "Could not locate #doesNotUnderstand:\n");
            exit(1);
        }
        
        // Protecting the selector pointer because it may be invalidated later
        hptr<TSymbol> failedSelector = newPointer(selector);
        
        // We're replacing the original call arguments with custom one
        hptr<TObjectArray> errorArguments = newObject<TObjectArray>(2);
        
        // Filling in the failed call context information
        errorArguments[0] = messageArguments[0]; // receiver object
        errorArguments[1] = failedSelector;      // message selector that failed
        
        // Replacing the arguments with newly created one
        messageArguments = errorArguments;
        
        // Continuing the execution just as if #doesNotUnderstand:
        // was the actual selector we wanted to call
    }
    
    // Save stack and opcode pointers
    ec.storePointers();
    
    // Create a new context for the giving method and arguments
    hptr<TContext> newContext   = newObject<TContext>();
    hptr<TObjectArray> newStack = newObject<TObjectArray>(getIntegerValue(receiverMethod->stackSize));
    hptr<TObjectArray> newTemps = newObject<TObjectArray>(getIntegerValue(receiverMethod->temporarySize));
    
    newContext->stack           = newStack;
    newContext->temporaries     = newTemps;
    newContext->arguments       = messageArguments;
    newContext->method          = receiverMethod;
    newContext->stackTop        = newInteger(0);
    newContext->bytePointer     = newInteger(0);
    
    // Suppose that current send message operation is last operation in the current context. 
    // If it is true then next instruction will be either stackReturn or blockReturn.
    // 
    // VM will switch to the newContext, perform it and then switch back to the current context
    // for the single one return instruction. This is pretty dumb to load the whole context 
    // just to exit it immediately. Therefore, we looking one instruction ahead to see if it is
    // a return instruction. If it is, we may skip our context and set our previousContext as 
    // previousContext for the newContext. In case of blockReturn it will be the previousContext
    // of the wrapping method context.
    
    uint8_t nextInstruction = ec.currentContext->method->byteCodes->getByte(ec.bytePointer);
    if (nextInstruction == (doSpecial * 16 + stackReturn)) {
        // Optimizing stack return
        newContext->previousContext = ec.currentContext->previousContext;
    } else if (nextInstruction == (doSpecial * 16 + blockReturn) &&
              (ec.currentContext->getClass() == globals.blockClass)) 
    {
        // Optimizing block return
        newContext->previousContext = ec.currentContext.cast<TBlock>()->creatingContext->previousContext;
    } else 
        newContext->previousContext = ec.currentContext;
    
    // Replace current context with the new one. On the next iteration, 
    // VM will start interpreting instructions from the new context.
    ec.currentContext = newContext;
    ec.loadPointers();
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
    bool  result = (top == globals.nilObject);

    if (ec.instruction.low != 0)
        result = not result;

    ec.returnedValue = result ? globals.trueObject : globals.falseObject;
    stack[ec.stackTop++] = ec.returnedValue;
}

void SmalltalkVM::doSendBinary(TVMExecutionContext& ec)
{
    TObjectArray& stack = * ec.currentContext->stack;
    
    // Loading operand objects
    TObject* rightObject = stack[--ec.stackTop];
    TObject* leftObject  = stack[--ec.stackTop];
    
    // If operands are both small integers, we need to handle it ourselves
    if (isSmallInteger(leftObject) && isSmallInteger(rightObject)) {
        // Loading actual operand values
        int32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
        int32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
        
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
                
            default:
                fprintf(stderr, "VM: Invalid opcode %d passed to sendBinary\n", ec.instruction.low);
                ec.returnedValue = globals.nilObject;
        }
        
        // Pushing result back to the stack
        stack[ec.stackTop++] = ec.returnedValue;
    } else {
        // This binary operator is performed on an ordinary object.
        // We do not know how to handle it, thus send the message to the receiver
        
        // Protecting pointers in case GC will occur
        hptr<TObject> pRightObject = newPointer(rightObject);
        hptr<TObject> pLeftObject  = newPointer(leftObject);
        
        hptr<TObjectArray> messageArguments = newObject<TObjectArray>(2, false);
        messageArguments[1] = pRightObject;
        messageArguments[0] = pLeftObject;
        
        TSymbol* messageSelector = (TSymbol*) globals.binaryMessages[ec.instruction.low];
        doSendMessage(ec, messageSelector, messageArguments);
    }
} 

SmalltalkVM::TExecuteResult SmalltalkVM::doDoSpecial(hptr<TProcess>& process, TVMExecutionContext& ec)
{
    TByteObject&  byteCodes         = * ec.currentContext->method->byteCodes;
    TObjectArray& stack             = * ec.currentContext->stack;
    TObjectArray& arguments         = * ec.currentContext->arguments;
    TSymbolArray& literals          = * ec.currentContext->method->literals;
    
    switch(ec.instruction.low) {
        case selfReturn: {
            ec.returnedValue  = arguments[0]; // self
            ec.currentContext = ec.currentContext->previousContext;
            if (ec.currentContext.rawptr() == 0 || ec.currentContext.rawptr() == globals.nilObject) {
                process->context  = ec.currentContext;
                process->result   = ec.returnedValue;
                return returnReturned;
            }
            
            ec.loadPointers();
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
        } break;
        
        case stackReturn:
        {
            ec.returnedValue  = stack[--ec.stackTop];
            ec.currentContext = ec.currentContext->previousContext;
            if (ec.currentContext.rawptr() == 0 || ec.currentContext.rawptr() == globals.nilObject) {
                process->context  = ec.currentContext;
                process->result   = ec.returnedValue;
                return returnReturned;
            }
            
            ec.loadPointers();
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
        } break;
        
        case blockReturn: {
            ec.returnedValue = stack[--ec.stackTop];
            TBlock* contextAsBlock = ec.currentContext.cast<TBlock>();
            ec.currentContext = contextAsBlock->creatingContext->previousContext;
            
            if (ec.currentContext.rawptr() == 0 || ec.currentContext.rawptr() == globals.nilObject) {
                process->context  = ec.currentContext;
                process->result   = ec.returnedValue;
                return returnReturned;
            }
            
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
            
            if (ec.returnedValue == globals.trueObject)
                ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            else
                ec.bytePointer += 2;
        } break;
        
        case branchIfFalse: {
            ec.returnedValue = stack[--ec.stackTop];
            
            if (ec.returnedValue == globals.falseObject)
                ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            else
                ec.bytePointer += 2;
        } break;
        
        case sendToSuper: {
            uint32_t literalIndex    = byteCodes[ec.bytePointer++];
            TSymbol* messageSelector = literals[literalIndex];
            TClass*  receiverClass   = ec.currentContext->method->klass->parentClass;
            TObjectArray* messageArguments = (TObjectArray*) stack[--ec.stackTop];
            
            doSendMessage(ec, messageSelector, messageArguments, receiverClass);
        } break;
        
        case breakpoint: {
            ec.bytePointer -= 1;
            
            // FIXME do not waste time to store process on the stack. we do not need it
            process->context = ec.currentContext;
            process->result  = ec.returnedValue;
            
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
            stack[ec.stackTop++] = reinterpret_cast<TObject*>(newInteger(constant));
            break;
            
        case nilConst:   stack[ec.stackTop++] = globals.nilObject;   break;
        case trueConst:  stack[ec.stackTop++] = globals.trueObject;  break;
        case falseConst: stack[ec.stackTop++] = globals.falseObject; break;
        default:
            /* TODO unknown push constant */ ;
            fprintf(stderr, "VM: unknown push constant %d\n", constant);
            stack[ec.stackTop++] = globals.nilObject;
    }
}

TObject* SmalltalkVM::doExecutePrimitive(uint8_t opcode, hptr<TProcess>& process, TVMExecutionContext& ec, bool* failed)
{
    TObjectArray& stack = *ec.currentContext->stack;
    
    if (failed)
        *failed = false;
    
    switch(opcode) {
        case objectsAreEqual: { // 1
            TObject* arg2 = stack[--ec.stackTop];
            TObject* arg1 = stack[--ec.stackTop];
            
            if (arg1 == arg2)
                return globals.trueObject;
            else
                return globals.falseObject;
        } break;
        
        case getClass: { // 2
            TObject* object = stack[--ec.stackTop];
            return isSmallInteger(object) ? globals.smallIntClass : object->getClass();
        } break;
        
        case ioPutChar: { // 3
            TInteger charObject = reinterpret_cast<TInteger>(stack[--ec.stackTop]);
            int8_t  charValue  = getIntegerValue(charObject);
            
            putchar(charValue);
            return globals.nilObject;
        } break;
        
        case ioGetChar: { // 9
            int32_t input = getchar();
            
            if (input == EOF)
                return globals.nilObject;
            else
                return reinterpret_cast<TObject*>(newInteger(input));
        } break;
        
        case getSize: {
            TObject* object     = stack[--ec.stackTop];
            uint32_t objectSize = isSmallInteger(object) ? 0 : object->getSize();
            
            return reinterpret_cast<TObject*>(newInteger(objectSize));
        } break;
        
        case 6: { // start new process
            TInteger  value = reinterpret_cast<TInteger>(stack[--ec.stackTop]);
            uint32_t  ticks = getIntegerValue(value);
            TProcess* newProcess = (TProcess*) stack[--ec.stackTop];
            
            // FIXME possible stack overflow due to recursive call
            TExecuteResult result = this->execute(newProcess, ticks);
            
            return reinterpret_cast<TObject*>(newInteger(result));
        } break;
        
        case allocateObject: { // 7
            // Taking object's size and class from the stack
            TObject* size  = stack[--ec.stackTop];
            TClass*  klass = (TClass*) stack[--ec.stackTop];
            uint32_t fieldsCount = getIntegerValue(reinterpret_cast<TInteger>(size));
            
            // Instantinating the object. Each object has size and class fields
            // which are not directly accessible in the managed code, so we need
            // to add 2 to the size known to the object itself to get the real size:
            
            return newOrdinaryObject(klass, sizeof(TObject) + fieldsCount * sizeof(TObject*)); 
        } break;
        
        case blockInvoke: { // 8
            TBlock*  block = (TBlock*) stack[--ec.stackTop];
            uint32_t argumentLocation = getIntegerValue(block->argumentLocation);
            
            // Amount of arguments stored on the stack except the block itself
            uint32_t argCount = ec.instruction.low - 1;
            
            // Checking the passed temps size
            TObjectArray* blockTemps = block->temporaries;
            
            if (argCount > (blockTemps ? blockTemps->getSize() - argumentLocation : 0) ) {
                ec.stackTop -= (argCount  + 1); // unrolling stack
                failPrimitive(stack, ec.stackTop, opcode);
                *failed = true;
                break;
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
                *failed = true;
                failPrimitive(stack, ec.stackTop, opcode);
                break;
            }
                
            // Extracting values
            int32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
            int32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
            
            // Performing an operation
            // The result may be nil if the opcode execution fails (division by zero etc)
            TObject* result = doSmallInt((SmallIntOpcode) opcode, leftOperand, rightOperand);
            if (result == globals.nilObject) {
                *failed = true;
                failPrimitive(stack, ec.stackTop, opcode);
                break;
            }
            return result;
        } break;
        
        // TODO case 18 // turn on debugging
        
        case 19: { // error
            process->context = ec.currentContext;
        } break;
        
        case allocateByteArray: { // 20
            int32_t dataSize = getIntegerValue(reinterpret_cast<TInteger>(stack[--ec.stackTop]));
            TClass* klass    = (TClass*) stack[--ec.stackTop];
            
            return newBinaryObject(klass, dataSize);
        } break;
        
        case arrayAt:      // 24 
        case arrayAtPut: { // 5
            TObject* indexObject = stack[--ec.stackTop];
            TObjectArray* array  = (TObjectArray*) stack[--ec.stackTop];
            TObject* valueObject = 0;
            
            // If the method is Array:at:put then pop a value from the stack
            if (opcode == arrayAtPut) 
                valueObject = stack[--ec.stackTop];
            
            if (! isSmallInteger(indexObject) ) {
                *failed = true;
                failPrimitive(stack, ec.stackTop, opcode);
                break;
            }

            // Smalltalk indexes arrays starting from 1, not from 0
            // So we need to recalculate the actual array index before
            uint32_t actualIndex = getIntegerValue(reinterpret_cast<TInteger>(indexObject)) - 1; 
            
            // Checking boundaries
            if (actualIndex >= array->getSize()) {
                *failed = true;
                failPrimitive(stack, ec.stackTop, opcode);
                break;
            }
            
            if (opcode == arrayAt) {
                return array->getField(actualIndex);
            } else { 
                // Array:at:put
                
                TObject*  oldValue   = array->getField(actualIndex);
                TObject** objectSlot = &( array->getFields()[actualIndex] );
                
                // Checking whether we need to register current object slot in the GC
                if (checkRoot(valueObject, oldValue, objectSlot))
                    printf("Root list altered for slot %p and value %p (old %p)\n",
                           objectSlot, valueObject, oldValue);
                    
                // Storing the value into the array
                array->putField(actualIndex, valueObject);
                
                // Return self
                return (TObject*) array;
            }
        } break;
        
        case stringAt:      // 21
        case stringAtPut: { // 22
            TObject* indexObject = stack[--ec.stackTop];
            TString* string      = (TString*) stack[--ec.stackTop];
            TObject* valueObject = 0;
            
            // If the method is String:at:put then pop a value from the stack
            if (opcode == stringAtPut) 
                valueObject = stack[--ec.stackTop];
            
            if (! isSmallInteger(indexObject)) {
                *failed = true;
                failPrimitive(stack, ec.stackTop, opcode);
                break;
            }
            
            // Smalltalk indexes arrays starting from 1, not from 0
            // So we need to recalculate the actual array index before
            uint32_t actualIndex = getIntegerValue(reinterpret_cast<TInteger>(indexObject)) - 1;
            
            // Checking boundaries
            if (actualIndex >= string->getSize()) {
                *failed = true;
                failPrimitive(stack, ec.stackTop, opcode);
                break;
            }
            
            if (opcode == stringAt) 
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
            TByteObject* clone = (TByteObject*) newBinaryObject(klass, dataSize);
            
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
                *failed = true;
                failPrimitive(stack, ec.stackTop, opcode);
                break;
            }
            
            TInteger integer = reinterpret_cast<TInteger>(object);
            int32_t  value   = getIntegerValue(integer);
            
            return reinterpret_cast<TObject*>(newInteger(value)); // FIXME long integer
        } break;
        
        case flushCache: // 34
            //FIXME returnedValue is not to be globals.nilObject
            flushMethodCache();
            *failed = false;
            break;
        
        case bulkReplace: { // 38
            //Implementation of replaceFrom:to:with:startingAt: as a primitive
            
            // Array replaceFrom: start to: stop with: replacement startingAt: repStart
            //      <38 start stop replacement repStart self>.
            
            // Current stack contents (top is the top)
            //      self
            //      repStart
            //      replacement
            //      stop
            //      start
            
            TObject* destination            = stack[--ec.stackTop];
            TObject* sourceStartOffset      = stack[--ec.stackTop];
            TObject* source                 = stack[--ec.stackTop];
            TObject* destinationStopOffset  = stack[--ec.stackTop];
            TObject* destinationStartOffset = stack[--ec.stackTop];
            
            bool isSucceeded = doBulkReplace( destination, destinationStartOffset, 
                                             destinationStopOffset, source, 
                                             sourceStartOffset );
            
            if (! isSucceeded) {
                *failed = true;
                failPrimitive(stack, ec.stackTop, opcode);
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
            fprintf(stderr, "VM: Unimplemented or invalid primitive %d\n", opcode);
        }
    }
    
    return globals.nilObject;
}

TObject* SmalltalkVM::doSmallInt( SmallIntOpcode opcode, int32_t leftOperand, int32_t rightOperand)
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
            
            int32_t result = 0;
            
            if (rightOperand < 0) {
                //shift right 
                result = leftOperand >> -rightOperand;
            } else {
                // shift left ; catch overflow 
                result = leftOperand << rightOperand;
                if (leftOperand > result) {
                    return globals.nilObject;
                }
            }
            
            return reinterpret_cast<TObject*>(newInteger(result));
        }
        
        default:
            fprintf(stderr, "VM: Invalid smallint opcode %d", opcode);
            return globals.nilObject; /* FIXME possible error */
    }
}

void SmalltalkVM::failPrimitive(TObjectArray& stack, uint32_t& stackTop, uint8_t opcode) {
    //printf("failPrimitive %d\n", opcode);
    //stack[stackTop++] = globals.nilObject;
}

void SmalltalkVM::onCollectionOccured()
{
    // Here we need to handle the GC collection event
    //printf("VM: GC had just occured. Flushing the method cache.\n");
    m_gcCount++;
    flushMethodCache();
}

bool SmalltalkVM::doBulkReplace( TObject* destination, TObject* destinationStartOffset, TObject* destinationStopOffset, TObject* source, TObject* sourceStartOffset) {
    
    if ( ! isSmallInteger(sourceStartOffset) || 
         ! isSmallInteger(destinationStartOffset) || 
         ! isSmallInteger(destinationStopOffset) )
        return false;

    // Smalltalk indexes are counted starting from 1. 
    // We need to decrement all values to get the zero based index:
    int32_t iSourceStartOffset      = getIntegerValue(reinterpret_cast<TInteger>(sourceStartOffset)) - 1; 
    int32_t iDestinationStartOffset = getIntegerValue(reinterpret_cast<TInteger>(destinationStartOffset)) - 1;
    int32_t iDestinationStopOffset  = getIntegerValue(reinterpret_cast<TInteger>(destinationStopOffset)) - 1;
    int32_t iCount                  = iDestinationStopOffset - iDestinationStartOffset + 1;
    
    if ( iSourceStartOffset      < 0 || 
         iDestinationStartOffset < 0 || 
         iDestinationStopOffset  < 0 || 
         iCount < 1 )
        return false;
    
    if (destination->getSize() < (uint32_t) iDestinationStopOffset || 
        source->getSize() < (uint32_t) (iSourceStartOffset + iCount) )
        return false;
    
    if ( source->isBinary() && destination->isBinary() ) {
        uint8_t* sourceBytes      = static_cast<TByteObject*>(source)->getBytes();
        uint8_t* destinationBytes = static_cast<TByteObject*>(destination)->getBytes();
        
        memcpy( & destinationBytes[iDestinationStartOffset], & sourceBytes[iSourceStartOffset], iCount );
        return true;
    }
    
    // If we're moving objects between static and dynamic memory, 
    // let the VM hadle it because pointer checking is required. See checkRoot()
    if (m_memoryManager->isInStaticHeap(source) != m_memoryManager->isInStaticHeap(destination))
        return false;
    
    if ( ! source->isBinary() && ! destination->isBinary() ) {
        // Interpreting pointer array as raw byte sequence
        uint8_t* sourceFields      = reinterpret_cast<uint8_t*>(source->getFields());
        uint8_t* destinationFields = reinterpret_cast<uint8_t*>(destination->getFields());
        
        memcpy( & destinationFields[iDestinationStartOffset], & sourceFields[iSourceStartOffset], iCount * sizeof(TObject*));
        return true;
    }
    
    return false;
}

void SmalltalkVM::printVMStat()
{
    float hitRatio = (float) 100 * m_cacheHits / (m_cacheHits + m_cacheMisses);
    printf("\nGC count: %d, cache hits: %d, misses: %d, hit ratio %.2f %%\n", 
        m_gcCount, m_cacheHits, m_cacheMisses, hitRatio);
}
