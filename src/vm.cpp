/*
 *    vm.cpp
 *
 *    Implementation of the virtual machine (SmalltalkVM class)
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

#include <vm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <opcodes.h>
#include <primitives.h>

#include <jit.h>

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

    // VM need to perform some actions if collection was occured.
    // First of all we need to invalidate the method cache
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

    // VM need to perform some actions if collection was occured.
    // First of all we need to invalidate the method cache
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

bool SmalltalkVM::checkRoot(TObject* value, TObject** objectSlot)
{
    // Here we need to perform some actions depending on whether the object slot and
    // the value resides. Generally, all pointers from the static heap to the dynamic one
    // should be tracked by the GC because it may be the only valid link to the object.
    // Object may be collected otherwise.

    bool valueIsStatic = m_memoryManager->isInStaticHeap(value);
    bool slotIsStatic  = m_memoryManager->isInStaticHeap(objectSlot);

    TObject* oldValue  = *objectSlot;

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
    
    entry.methodName    = selector;
    entry.receiverClass = klass;
    entry.method        = method;
}

TMethod* SmalltalkVM::lookupMethod(TSymbol* selector, TClass* klass)
{
    // First of all checking the method cache
    // Frequently called methods most likely will be there
    TMethod* method = lookupMethodInCache(selector, klass);
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

SmalltalkVM::TExecuteResult SmalltalkVM::execute(TProcess* p, uint32_t ticks)
{
    // Protecting the process pointer
    hptr<TProcess> currentProcess = newPointer(p);
    
    // Initializing an execution context
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
        
        // Decoding the instruction
        ec.instruction.low = (ec.instruction.high = byteCodes[ec.bytePointer++]) & 0x0F;
        ec.instruction.high >>= 4;
        if (ec.instruction.high == opcode::extended) {
            ec.instruction.high = ec.instruction.low;
            ec.instruction.low  = byteCodes[ec.bytePointer++];
        }
        
        // And executing it
        switch (ec.instruction.high) {
            case opcode::pushInstance:    stack[ec.stackTop++] = instanceVariables[ec.instruction.low]; break;
            case opcode::pushArgument:    stack[ec.stackTop++] = arguments[ec.instruction.low];         break;
            case opcode::pushTemporary:   stack[ec.stackTop++] = temporaries[ec.instruction.low];       break;
            case opcode::pushLiteral:     stack[ec.stackTop++] = literals[ec.instruction.low];          break;
            case opcode::pushConstant:    doPushConstant(ec);                                           break;
            case opcode::pushBlock:       doPushBlock(ec);                                              break;
            
            case opcode::assignTemporary: temporaries[ec.instruction.low] = stack[ec.stackTop - 1];     break;
            case opcode::assignInstance: {
                TObject*  newValue   =   stack[ec.stackTop - 1];
                TObject** objectSlot = & instanceVariables[ec.instruction.low];
                
                // Checking whether we need to register current object slot in the GC
                checkRoot(newValue, objectSlot);

                // Performing an assignment
                instanceVariables[ec.instruction.low] = newValue;
            } break;
            
            case opcode::markArguments: doMarkArguments(ec); break;
            
            case opcode::sendMessage:   doSendMessage(ec);   break;
            case opcode::sendUnary:     doSendUnary(ec);     break;
            case opcode::sendBinary:    doSendBinary(ec);    break;
            
            case opcode::doPrimitive: {
                TExecuteResult result = doPrimitive(currentProcess, ec);
                if (result != returnNoReturn)
                    return result;
            } break;
            
            case opcode::doSpecial: {
                TExecuteResult result = doSpecial(currentProcess, ec);
                if (result != returnNoReturn)
                    return result;
            } break;
            
            default:
                fprintf(stderr, "VM: Invalid opcode %d at offset %d in method ", ec.instruction.high, ec.bytePointer);
                fprintf(stderr, "'%s'\n", ec.currentContext->method->name->toString().c_str() );
                exit(1);
        }
    }
}

void SmalltalkVM::doPushBlock(TVMExecutionContext& ec)
{
    TByteObject&  byteCodes  = * ec.currentContext->method->byteCodes;
    hptr<TObjectArray> stack = newPointer(ec.currentContext->stack);
    
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
    
    //We set block->bytePointer, stackTop, previousContext when block is invoked
    
    // Assigning creatingContext depending on the hierarchy
    // Nested blocks inherit the outer creating context
    if (ec.currentContext->getClass() == globals.blockClass)
        newBlock->creatingContext = ec.currentContext.cast<TBlock>()->creatingContext;
    else
        newBlock->creatingContext = ec.currentContext;
    
    // Inheriting the context objects
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
    TObjectArray& stack      = * ec.currentContext->stack;
    
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
    
    hptr<TMethod> receiverMethod = newPointer(lookupMethod(selector, receiverClass));
    
    // Checking whether we found a method
    if (receiverMethod == 0) {
        // Oops. Method was not found. In this case we should send #doesNotUnderstand: message to the receiver
        setupVarsForDoesNotUnderstand(receiverMethod, messageArguments, selector, receiverClass);
        // Continuing the execution just as if #doesNotUnderstand: was the actual selector that we wanted to call
    }
    
    // Save stack and opcode pointers
    ec.storePointers();
    
    // Create a new context for the giving method and arguments
    hptr<TContext>   newContext = newObject<TContext>();
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
    if (nextInstruction == (opcode::doSpecial * 16 + special::stackReturn)) {
        // Optimizing stack return
        newContext->previousContext = ec.currentContext->previousContext;
    } else if (nextInstruction == (opcode::doSpecial * 16 + special::blockReturn) &&
              (ec.currentContext->getClass() == globals.blockClass))
    {
        // Optimizing block return
        newContext->previousContext = ec.currentContext.cast<TBlock>()->creatingContext->previousContext;
    } else {
        newContext->previousContext = ec.currentContext;
    }
    // Replace current context with the new one. On the next iteration,
    // VM will start interpreting instructions from the new context.
    ec.currentContext = newContext;
    ec.loadPointers();
    
    m_messagesSent++;
}

void SmalltalkVM::setupVarsForDoesNotUnderstand(hptr<TMethod>& method, hptr<TObjectArray>& arguments, TSymbol* selector, TClass* receiverClass) {
    // Looking up the #doesNotUnderstand: method:
    method = newPointer(lookupMethod(globals.badMethodSymbol, receiverClass));
    if (method == 0) {
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
    errorArguments[0] = arguments[0];   // receiver object
    errorArguments[1] = failedSelector; // message selector that failed
    
    // Replacing the arguments with newly created one
    arguments = errorArguments;
}

void SmalltalkVM::doSendMessage(TVMExecutionContext& ec)
{
    TObjectArray& stack = * ec.currentContext->stack;
    TObjectArray* messageArguments = (TObjectArray*) stack[--ec.stackTop];
    
    // These do not need to be hptr'ed
    TSymbolArray& literals   = * ec.currentContext->method->literals;
    TSymbol* messageSelector = literals[ec.instruction.low];
    
    doSendMessage(ec, messageSelector, messageArguments);
}

void SmalltalkVM::doSendUnary(TVMExecutionContext& ec)
{
    TObjectArray& stack = *ec.currentContext->stack;
    TObject*        top = stack[--ec.stackTop];
    
    switch ((unaryMessage::Opcode) ec.instruction.low) {
        case unaryMessage::isNil  : ec.returnedValue = (top == globals.nilObject) ? globals.trueObject : globals.falseObject; break;
        case unaryMessage::notNil : ec.returnedValue = (top != globals.nilObject) ? globals.trueObject : globals.falseObject; break;
        
        default:
            fprintf(stderr, "VM: Invalid opcode %d passed to sendUnary\n", ec.instruction.low);
            exit(1);
    }
    
    stack[ec.stackTop++] = ec.returnedValue;
    
    m_messagesSent++;
}

void SmalltalkVM::doSendBinary(TVMExecutionContext& ec)
{
    TObjectArray& stack = * ec.currentContext->stack;
    
    // Loading the operand objects
    TObject* rightObject = stack[--ec.stackTop];
    TObject* leftObject  = stack[--ec.stackTop];
    
    // If operands are both small integers, we may handle it ourselves
    if (isSmallInteger(leftObject) && isSmallInteger(rightObject)) {
        // Loading actual operand values
        int32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
        int32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
        bool unusedCondition;
        
        // Performing an operation
        switch ((binaryMessage::Operator) ec.instruction.low) {
            case binaryMessage::operatorLess:
                ec.returnedValue = (leftOperand < rightOperand) ? globals.trueObject : globals.falseObject;
                break;
            
            case binaryMessage::operatorLessOrEq:
                ec.returnedValue = (leftOperand <= rightOperand) ? globals.trueObject : globals.falseObject;
                break;
            
            case binaryMessage::operatorPlus:
                ec.returnedValue = callSmallIntPrimitive(primitive::smallIntAdd, leftOperand, rightOperand, unusedCondition);
                break;
            
            default:
                fprintf(stderr, "VM: Invalid opcode %d passed to sendBinary\n", ec.instruction.low);
                exit(1);
        }
        
        // Pushing result back to the stack
        stack[ec.stackTop++] = ec.returnedValue;
        m_messagesSent++;
    } else {
        // This binary operator is performed on an ordinary object.
        // We do not know how to handle it, thus send the message to the receiver
        
        // Protecting pointers in case if GC occurs
        hptr<TObject> pRightObject = newPointer(rightObject);
        hptr<TObject> pLeftObject  = newPointer(leftObject);
        
        hptr<TObjectArray> messageArguments = newObject<TObjectArray>(2, false);
        messageArguments[1] = pRightObject;
        messageArguments[0] = pLeftObject;
        
        TSymbol* messageSelector = (TSymbol*) globals.binaryMessages[ec.instruction.low];
        doSendMessage(ec, messageSelector, messageArguments);
    }
}

SmalltalkVM::TExecuteResult SmalltalkVM::doSpecial(hptr<TProcess>& process, TVMExecutionContext& ec)
{
    TByteObject&  byteCodes  = * ec.currentContext->method->byteCodes;
    TObjectArray& stack      = * ec.currentContext->stack;
    TObjectArray& arguments  = * ec.currentContext->arguments;
    TSymbolArray& literals   = * ec.currentContext->method->literals;
    
    switch(ec.instruction.low) {
        case special::selfReturn: {
            ec.returnedValue  = arguments[0]; // arguments[0] always keep self
            ec.currentContext = ec.currentContext->previousContext;
            
            if (ec.currentContext.rawptr() == globals.nilObject) {
                process->context = ec.currentContext;
                process->result  = ec.returnedValue;
                return returnReturned;
            }
            
            ec.loadPointers();
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
        } break;
        
        case special::stackReturn: {
            ec.returnedValue  = stack[--ec.stackTop];
            ec.currentContext = ec.currentContext->previousContext;
            
            if (ec.currentContext.rawptr() == globals.nilObject) {
                process->context = ec.currentContext;
                process->result  = ec.returnedValue;
                return returnReturned;
            }
            
            ec.loadPointers();
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
        } break;
        
        case special::blockReturn: {
            ec.returnedValue = stack[--ec.stackTop];
            TBlock* contextAsBlock = ec.currentContext.cast<TBlock>();
            ec.currentContext = contextAsBlock->creatingContext->previousContext;
            
            if (ec.currentContext.rawptr() == globals.nilObject) {
                process->context = ec.currentContext;
                process->result  = ec.returnedValue;
                return returnReturned;
            }
            
            ec.loadPointers();
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
        } break;
        
        case special::duplicate: {
            // Duplicate an object on the stack
            TObject* copy = stack[ec.stackTop - 1];
            stack[ec.stackTop++] = copy;
        } break;
        
        case special::popTop:
            ec.stackTop--;
            break;
        
        case special::branch:
            ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            break;
        
        case special::branchIfTrue: {
            ec.returnedValue = stack[--ec.stackTop];
            
            if (ec.returnedValue == globals.trueObject)
                ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            else
                ec.bytePointer += 2;
        } break;
        
        case special::branchIfFalse: {
            ec.returnedValue = stack[--ec.stackTop];
            
            if (ec.returnedValue == globals.falseObject)
                ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            else
                ec.bytePointer += 2;
        } break;
        
        case special::sendToSuper: {
            uint32_t literalIndex    = byteCodes[ec.bytePointer++];
            TSymbol* messageSelector = literals[literalIndex];
            TClass*  receiverClass   = ec.currentContext->method->klass->parentClass;
            TObjectArray* messageArguments = (TObjectArray*) stack[--ec.stackTop];
            
            doSendMessage(ec, messageSelector, messageArguments, receiverClass);
        } break;
        
        case special::breakpoint: {
            ec.bytePointer -= 1;
            
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
        
        case pushConstants::nil:         stack[ec.stackTop++] = globals.nilObject;   break;
        case pushConstants::trueObject:  stack[ec.stackTop++] = globals.trueObject;  break;
        case pushConstants::falseObject: stack[ec.stackTop++] = globals.falseObject; break;
        default:
            fprintf(stderr, "VM: unknown push constant %d\n", constant);
            exit(1);
    }
}

SmalltalkVM::TExecuteResult SmalltalkVM::doPrimitive(hptr<TProcess>& process, TVMExecutionContext& ec)
{
    TObjectArray& stack = *ec.currentContext->stack;
    uint8_t      opcode = (*ec.currentContext->method->byteCodes)[ec.bytePointer++];
    
    // First of all, executing the primitive
    // If primitive succeeds then stop execution of the current method
    //   and push the result onto the stack of the previous context
    //
    // If primitive fails, execution flow resumes in the current method after the primitive call.
    //
    // NOTE: Some primitives do not affect the execution
    //       context. These are handled separately in the
    //       bottom of the current function

    bool failed = false;
    ec.returnedValue = performPrimitive(opcode, process, ec, failed);
    
    //LLVMsendMessage primitive may fail only if during execution throwError was called
    if (failed && opcode == primitive::LLVMsendMessage) {
        return returnError;
    }
    //If primitive failed during execution we should continue execution in the current method
    if (failed) {
        stack[ec.stackTop++] = globals.nilObject;
        return returnNoReturn;
    }
    
    // primitiveNumber exceptions:
    // 19 - error trap
    // 8  - block invocation
    
    switch (opcode) {
        case 255:
            // Debug break
            // Leave the context alone
            break;
            
        case primitive::throwError: // 19
            return returnError;
        
        case primitive::blockInvoke: // 8
            // We do not want to leave the block context which was just loaded
            // So we're continuing without context switching
            break;
        
        default:
            // We have executed a primitive. Now we have to reject the current
            // execution context and push the result onto the previous context's stack
            ec.currentContext = ec.currentContext->previousContext;
            
            if (ec.currentContext.rawptr() == globals.nilObject) {
                process->context = ec.currentContext;
                process->result  = ec.returnedValue;
                return returnReturned;
            }
            
            // Inject the result...
            ec.stackTop = getIntegerValue(ec.currentContext->stackTop);
            (*ec.currentContext->stack)[ec.stackTop++] = ec.returnedValue;
            
            // Save the stack pointer
            ec.currentContext->stackTop = newInteger(ec.stackTop);
            
            ec.bytePointer = getIntegerValue(ec.currentContext->bytePointer);
    }
    
    return returnNoReturn;
}

// TODO Refactor code to make this clean
extern "C" { TObject* sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass); }

TObject* SmalltalkVM::performPrimitive(uint8_t opcode, hptr<TProcess>& process, TVMExecutionContext& ec, bool& failed) {
    TObjectArray& stack = *ec.currentContext->stack;
    
    switch (opcode) {
        // FIXME opcodes 253-255 are not standard
        case 255:
            // Debug trap
            printf("Debug trap\n");
            break;
        
        case 254:
            m_memoryManager->collectGarbage();
            break;
        
        case primitive::LLVMsendMessage: { //252
            TObjectArray* args = (TObjectArray*) stack[--ec.stackTop];
            TSymbol*  selector = (TSymbol*) stack[--ec.stackTop];
            try {
                return sendMessage(ec.currentContext, selector, args, 0);
            } catch(TBlockReturn blockReturn) {
                //When we catch blockReturn we change the current context to block.creatingContext.
                //The post processing code will change 'block.creatingContext' to the previous one
                // and the result of blockReturn will be injected on the stack
                ec.currentContext = blockReturn.targetContext;
                return blockReturn.value;
            } catch(TContext* errorContext) {
                //context is thrown by LLVM:throwError primitive
                //that means that it was not caught by LLVM:executeProcess function
                //so we have to stop execution of the current process and return returnError
                process->context = errorContext;
                failed = true;
                return globals.nilObject;
            }
        } break;
        
        case 251: {
            // TODO Unicode support
            
            TString* prompt = (TString*) stack[--ec.stackTop];
            std::string strPrompt((const char*)prompt->getBytes(), prompt->getSize());
            
            char* input = readline(strPrompt.c_str());
            if (input) {
                uint32_t inputSize = strlen(input);
                
                if (inputSize > 0)
                    add_history(input);
                    
                TString* result = (TString*) newBinaryObject(globals.stringClass, inputSize);
                memcpy(result->getBytes(), input, inputSize);
                
                free(input);
                return result;
            } else
                return globals.nilObject;
        } break;
        
        case primitive::startNewProcess: { // 6
            TInteger  value = reinterpret_cast<TInteger>(stack[--ec.stackTop]);
            uint32_t  ticks = getIntegerValue(value);
            TProcess* newProcess = (TProcess*) stack[--ec.stackTop];
            
            // FIXME possible stack overflow due to recursive call
            TExecuteResult result = this->execute(newProcess, ticks);
            
            return reinterpret_cast<TObject*>(newInteger(result));
        } break;
        
        case primitive::allocateObject: { // 7
            // Taking object's size and class from the stack
            TObject* size  = stack[--ec.stackTop];
            TClass*  klass = (TClass*) stack[--ec.stackTop];
            uint32_t fieldsCount = getIntegerValue(reinterpret_cast<TInteger>(size));
            
            // Instantinating the object. Each object has size and class fields
            
            return newOrdinaryObject(klass, sizeof(TObject) + fieldsCount * sizeof(TObject*));
        } break;

        case primitive::blockInvoke: { // 8
            TBlock*  block = (TBlock*) stack[--ec.stackTop];
            uint32_t argumentLocation = getIntegerValue(block->argumentLocation);
            
            // Amount of arguments stored on the stack except the block itself
            uint32_t argCount = ec.instruction.low - 1;
            
            // Checking the passed temps size
            TObjectArray* blockTemps = block->temporaries;
            
            if (argCount > (blockTemps ? blockTemps->getSize() - argumentLocation : 0) ) {
                ec.stackTop -= (argCount  + 1); // unrolling stack
                failed = true;
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
        
        case primitive::throwError: // 19
            process->context = ec.currentContext;
            process->result  = ec.returnedValue;
            break;
        
        case primitive::allocateByteArray: { // 20
            int32_t dataSize = getIntegerValue(reinterpret_cast<TInteger>(stack[--ec.stackTop]));
            TClass* klass    = (TClass*) stack[--ec.stackTop];
            
            return newBinaryObject(klass, dataSize);
        } break;
        
        case primitive::arrayAt:      // 24
        case primitive::arrayAtPut: { // 5
            TObject* indexObject = stack[--ec.stackTop];
            TObjectArray* array  = (TObjectArray*) stack[--ec.stackTop];
            TObject* valueObject = 0;
            
            // If the method is Array:at:put then pop a value from the stack
            if (opcode == primitive::arrayAtPut)
                valueObject = stack[--ec.stackTop];
            
            if (! isSmallInteger(indexObject) ) {
                failed = true;
                break;
            }
            
            // Smalltalk indexes arrays starting from 1, not from 0
            // So we need to recalculate the actual array index before
            uint32_t actualIndex = getIntegerValue(reinterpret_cast<TInteger>(indexObject)) - 1;
            
            // Checking boundaries
            if (actualIndex >= array->getSize()) {
                failed = true;
                break;
            }
            
            if (opcode == primitive::arrayAt) {
                return array->getField(actualIndex);
            } else {
                // Array:at:put
                
                TObject** objectSlot = &( array->getFields()[actualIndex] );
                
                // Checking whether we need to register current object slot in the GC
                checkRoot(valueObject, objectSlot);
                
                // Storing the value into the array
                array->putField(actualIndex, valueObject);
                
                // Return self
                return (TObject*) array;
            }
        } break;
        
        case primitive::cloneByteObject: { // 23
            TClass* klass = (TClass*) stack[--ec.stackTop];
            hptr<TByteObject> original = newPointer((TByteObject*) stack[--ec.stackTop]);
            
            // Creating clone
            uint32_t dataSize  = original->getSize();
            TByteObject* clone = (TByteObject*) newBinaryObject(klass, dataSize);
            
            // Cloning data
            memcpy(clone->getBytes(), original->getBytes(), dataSize);
            return (TObject*) clone;
        } break;
        
        //         case primitive::integerDiv:   // Integer /
        //         case primitive::integerMod:   // Integer %
        //         case primitive::integerAdd:   // Integer +
        //         case primitive::integerMul:   // Integer *
        //         case primitive::integerSub:   // Integer -
        //         case primitive::integerLess:  // Integer <
        //         case primitive::integerEqual: // Integer ==
        //             //TODO integer operations
        //             break;
        
        case primitive::integerNew: { // 32
            TObject* object = stack[--ec.stackTop];
            if (! isSmallInteger(object)) {
                failed = true;
                break;
            }
            
            TInteger integer = reinterpret_cast<TInteger>(object);
            int32_t  value   = getIntegerValue(integer);
            
            return reinterpret_cast<TObject*>(newInteger(value)); // FIXME long integer
        } break;
        
        case primitive::flushCache: // 34
            flushMethodCache();
            break;
        
        case primitive::bulkReplace: { // 38
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
            
            bool isSucceeded = doBulkReplace( destination, destinationStartOffset, destinationStopOffset, source, sourceStartOffset );
            
            if (! isSucceeded) {
                failed = true;
                break;
            }
            return destination;
        } break;
        
        // TODO cases 33, 35, 40
        // TODO case 18 // turn on debugging
        
        case primitive::objectsAreEqual:    // 1
        case primitive::getClass:           // 2
        case primitive::getSize:            // 4
        
        case primitive::ioPutChar:          // 3
        case primitive::ioGetChar:          // 9
        
        case primitive::stringAt:           // 21
        case primitive::stringAtPut:        // 22
        
        case primitive::smallIntAdd:        // 10
        case primitive::smallIntDiv:        // 11
        case primitive::smallIntMod:        // 12
        case primitive::smallIntLess:       // 13
        case primitive::smallIntEqual:      // 14
        case primitive::smallIntMul:        // 15
        case primitive::smallIntSub:        // 16
        case primitive::smallIntBitOr:      // 36
        case primitive::smallIntBitAnd:     // 37
        case primitive::smallIntBitShift:   // 39
        
        case primitive::getSystemTicks:     //253
        
        default: {
            hptr<TObjectArray> pStack = newPointer(ec.currentContext->stack);
            
            uint32_t argCount = ec.instruction.low;
            hptr<TObjectArray> args = newObject<TObjectArray>(argCount);
            
            uint32_t i = argCount;
            while (i > 0)
                args[--i] = pStack[--ec.stackTop];
            
            TObject* result = callPrimitive(opcode, args, failed);
            return result;
        }
    }
    
    return globals.nilObject;
}

void SmalltalkVM::onCollectionOccured()
{
    // Here we need to handle the GC collection event
    //printf("VM: GC had just occured. Flushing the method cache.\n");
    flushMethodCache();
}

bool SmalltalkVM::doBulkReplace( TObject* destination, TObject* destinationStartOffset, TObject* destinationStopOffset, TObject* source, TObject* sourceStartOffset) {

    if ( ! isSmallInteger(sourceStartOffset) ||
         ! isSmallInteger(destinationStartOffset) ||
         ! isSmallInteger(destinationStopOffset) )
    {
        return false;
    }
    
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
    {
        return false;
    }
    
    if (destination->getSize() < (uint32_t) iDestinationStopOffset ||
        source->getSize() < (uint32_t) (iSourceStartOffset + iCount) )
    {
        return false;
    }
    
    if ( source->isBinary() && destination->isBinary() ) {
        // Interpreting pointer array as raw byte sequence
        uint8_t* sourceBytes      = static_cast<TByteObject*>(source)->getBytes();
        uint8_t* destinationBytes = static_cast<TByteObject*>(destination)->getBytes();
        
        // Primitive may be called on the same object, so memory overlapping may occur.
        // memmove() works much like the ordinary memcpy() except that it correctly
        // handles the case with overlapping memory areas
        memmove( & destinationBytes[iDestinationStartOffset], & sourceBytes[iSourceStartOffset], iCount );
        return true;
    }
    
    // If we're moving objects between static and dynamic memory,
    // let the VM hadle it because pointer checking is required. See checkRoot()
    if (m_memoryManager->isInStaticHeap(source) != m_memoryManager->isInStaticHeap(destination))
    {
        return false;
    }
    
    if ( ! source->isBinary() && ! destination->isBinary() ) {
        TObject** sourceFields      = source->getFields();
        TObject** destinationFields = destination->getFields();
        
        // Primitive may be called on the same object, so memory overlapping may occur.
        // memmove() works much like the ordinary memcpy() except that it correctly
        // handles the case with overlapping memory areas
        memmove( & destinationFields[iDestinationStartOffset], & sourceFields[iSourceStartOffset], iCount * sizeof(TObject*) );
        return true;
    }
    
    return false;
}

void SmalltalkVM::printVMStat()
{
    float hitRatio = (float) 100 * m_cacheHits / (m_cacheHits + m_cacheMisses);
    printf("%d messages sent, cache hits: %d, misses: %d, hit ratio %.2f %%\n",
        m_messagesSent, m_cacheHits, m_cacheMisses, hitRatio);
}
