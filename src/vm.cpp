/*
 *    vm.cpp
 *
 *    Implementation of the virtual machine (SmalltalkVM class)
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.2
 *
 *    LLST is
 *        Copyright (C) 2012-2013 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2013 by Roman Proskuryakov <humbug@deeptown.org>
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


#include <readline/readline.h>
#include <readline/history.h>
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <cstring>

#include <opcodes.h>
#include <primitives.h>
#include <vm.h>
#include <api.h>

#if defined(LLVM)
    #include <jit.h>
#endif

TObject* SmalltalkVM::newOrdinaryObject(TClass* klass, std::size_t slotSize)
{
    // Class may be moved during GC in allocation,
    // so we need to protect the pointer
    hptr<TClass> pClass = newPointer(klass);

    void* objectSlot = m_memoryManager->allocate(slotSize, &m_lastGCOccured);
    if (!objectSlot) {
        std::fprintf(stderr, "VM: memory manager failed to allocate %u bytes\n", slotSize);
        return globals.nilObject;
    }

    // VM need to perform some actions if collection was occured.
    // First of all we need to invalidate the method cache
    if (m_lastGCOccured)
        onCollectionOccured();

    // Object size stored in the TSize field of any ordinary object contains
    // number of pointers except for the first two fields
    std::size_t fieldsCount = slotSize / sizeof(TObject*) - 2;

    TObject* instance = new (objectSlot) TObject(fieldsCount, pClass);

    for (uint32_t index = 0; index < fieldsCount; index++)
        instance->putField(index, globals.nilObject);

    return instance;
}

TByteObject* SmalltalkVM::newBinaryObject(TClass* klass, std::size_t dataSize)
{
    // Class may be moved during GC in allocation,
    // so we need to protect the pointer
    hptr<TClass> pClass = newPointer(klass);

    // All binary objects are descendants of ByteObject
    // They could not have ordinary fields, so we may use it
    uint32_t slotSize = sizeof(TByteObject) + correctPadding(dataSize);

    void* objectSlot = m_memoryManager->allocate(slotSize, &m_lastGCOccured);
    if (!objectSlot) {
        std::fprintf(stderr, "VM: memory manager failed to allocate %d bytes\n", slotSize);
        return static_cast<TByteObject*>(globals.nilObject);
    }

    // VM need to perform some actions if collection was occured.
    // First of all we need to invalidate the method cache
    if (m_lastGCOccured)
        onCollectionOccured();

    TByteObject* instance = new (objectSlot) TByteObject(dataSize, pClass);

    return instance;
}

template<> hptr<TObjectArray> SmalltalkVM::newObject<TObjectArray>(std::size_t dataSize, bool registerPointer)
{
    TClass* klass = globals.arrayClass;
    TObjectArray* instance = static_cast<TObjectArray*>( newOrdinaryObject(klass, sizeof(TObjectArray) + dataSize * sizeof(TObject*)) );
    return hptr<TObjectArray>(instance, m_memoryManager, registerPointer);
}

template<> hptr<TSymbolArray> SmalltalkVM::newObject<TSymbolArray>(std::size_t dataSize, bool registerPointer)
{
    TClass* klass = globals.arrayClass;
    TSymbolArray* instance = static_cast<TSymbolArray*>( newOrdinaryObject(klass, sizeof(TSymbolArray) + dataSize * sizeof(TObject*)) );
    return hptr<TSymbolArray>(instance, m_memoryManager, registerPointer);
}

template<> hptr<TContext> SmalltalkVM::newObject<TContext>(std::size_t dataSize, bool registerPointer)
{
    TClass* klass = globals.contextClass;
    TContext* instance = static_cast<TContext*>( newOrdinaryObject(klass, sizeof(TContext)) );
    return hptr<TContext>(instance, m_memoryManager, registerPointer);
}

template<> hptr<TBlock> SmalltalkVM::newObject<TBlock>(std::size_t dataSize, bool registerPointer)
{
    TClass* klass = globals.blockClass;
    TBlock* instance = static_cast<TBlock*>( newOrdinaryObject(klass, sizeof(TBlock)) );
    return hptr<TBlock>(instance, m_memoryManager, registerPointer);
}

void SmalltalkVM::TVMExecutionContext::stackPush(TObject* object)
{
    assert(object != 0);
    //NOTE: boundary check
    //      The Timothy A. Budd's version of compiler produces
    //      bytecode which can overflow the stack of the context
    {
        uint32_t stackSize = currentContext->stack->getSize();
        if( stackTop >= stackSize ) {
            //resize the current stack
            hptr<TObjectArray> newStack = m_vm->newObject<TObjectArray>(stackSize+5);
            for(uint32_t i = 0; i < stackSize; i++) {
                TObject* value = currentContext->stack->getField(i);
                newStack->putField(i, value);
            }
            currentContext->stack = newStack;
            std::cerr << std::endl << "VM: Stack overflow in '" << currentContext->method->name->toString() << "'" << std::endl;
        }
    }
    currentContext->stack->putField(stackTop++, object);
}

bool SmalltalkVM::checkRoot(TObject* value, TObject** objectSlot)
{
    return m_memoryManager->checkRoot(value, objectSlot);
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
    assert(selector != 0);
    assert(klass != 0);
    // First of all checking the method cache
    // Frequently called methods most likely will be there
    TMethod* method = lookupMethodInCache(selector, klass);
    if (method)
        return method; // We're lucky!

    // Well, maybe we'll be luckier next time. For now we need to do the full search.
    // Scanning through the class hierarchy from the klass up to the Object
    for (TClass* currentClass = klass; currentClass != globals.nilObject; currentClass = currentClass->parentClass) {
        assert(currentClass != 0);
        TDictionary* methods = currentClass->methods;
        method = methods->find<TMethod>(selector);
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
    for (std::size_t i = 0; i < LOOKUP_CACHE_SIZE; i++)
        m_lookupCache[i].methodName = 0;
}

SmalltalkVM::TExecuteResult SmalltalkVM::execute(TProcess* p, uint32_t ticks)
{
    // Protecting the process pointer
    hptr<TProcess> currentProcess = newPointer(p);

    assert(currentProcess->context != 0);
    assert(currentProcess->context->method != 0);

    // Initializing an execution context
    TVMExecutionContext ec(m_memoryManager, this);
    ec.currentContext = currentProcess->context;
    ec.loadPointers(); // Loads bytePointer & stackTop

    while (true)
    {
        assert(ec.currentContext != 0);
        assert(ec.currentContext->method != 0);
        assert(ec.currentContext->stack != 0);
        assert(ec.bytePointer <= ec.currentContext->method->byteCodes->getSize());
        assert(ec.currentContext->arguments->getSize() >= 1);
        assert(ec.currentContext->arguments->getField(0) != 0);

        // Initializing helper references
        TByteObject&  byteCodes = * ec.currentContext->method->byteCodes;

        TObjectArray& temporaries       = * ec.currentContext->temporaries;
        TObjectArray& arguments         = * ec.currentContext->arguments;
        TObjectArray& instanceVariables = * arguments.getField<TObjectArray>(0);
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
            case opcode::pushInstance:    ec.stackPush(instanceVariables[ec.instruction.low]); break;
            case opcode::pushArgument:    ec.stackPush(arguments[ec.instruction.low]);         break;
            case opcode::pushTemporary:   ec.stackPush(temporaries[ec.instruction.low]);       break;
            case opcode::pushLiteral:     ec.stackPush(literals[ec.instruction.low]);          break;
            case opcode::pushConstant:    doPushConstant(ec);                                  break;
            case opcode::pushBlock:       doPushBlock(ec);                                     break;

            case opcode::assignTemporary: temporaries[ec.instruction.low] = ec.stackLast();    break;
            case opcode::assignInstance: {
                TObject*  newValue   =   ec.stackLast();
                TObject** objectSlot = & instanceVariables[ec.instruction.low];

                // Checking whether we need to register current object slot in the GC
                checkRoot(newValue, objectSlot);

                // Performing the assignment
                *objectSlot = newValue;
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
                std::fprintf(stderr, "VM: Invalid opcode %d at offset %d in method ", ec.instruction.high, ec.bytePointer);
                std::fprintf(stderr, "'%s'\n", ec.currentContext->method->name->toString().c_str() );
                std::exit(1);
        }
    }
}

void SmalltalkVM::doPushBlock(TVMExecutionContext& ec)
{
    TByteObject&  byteCodes  = * ec.currentContext->method->byteCodes;

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
    newBlock->stack    = newObject<TObjectArray>(stackSize/*, false*/);

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
    ec.stackPush(newBlock);
}

void SmalltalkVM::doMarkArguments(TVMExecutionContext& ec)
{
    hptr<TObjectArray> args  = newObject<TObjectArray>(ec.instruction.low);

    // This operation takes instruction.low arguments
    // from the top of the stack and creates new array with them

    uint32_t index = ec.instruction.low;
    while (index > 0)
        args[--index] = ec.stackPop();

    ec.stackPush(args);
}

void SmalltalkVM::doSendMessage(TVMExecutionContext& ec, TSymbol* selector, TObjectArray* arguments, TClass* receiverClass /*= 0*/ )
{
    hptr<TObjectArray> messageArguments = newPointer(arguments);

    if (!receiverClass) {
        TObject* receiver = messageArguments[0];
        assert(receiver != 0);
        receiverClass = isSmallInteger(receiver) ? globals.smallIntClass : receiver->getClass();
        assert(receiverClass != 0);
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
        std::fprintf(stderr, "Could not locate #doesNotUnderstand:\n");
        //exit(1);
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
    TObjectArray* messageArguments = ec.stackPop<TObjectArray>();

    // These do not need to be hptr'ed
    TSymbolArray& literals   = * ec.currentContext->method->literals;
    TSymbol* messageSelector = literals[ec.instruction.low];

    doSendMessage(ec, messageSelector, messageArguments);
}

void SmalltalkVM::doSendUnary(TVMExecutionContext& ec)
{
    TObject* top = ec.stackPop();

    switch ( static_cast<unaryBuiltIns::Opcode>(ec.instruction.low) ) {
        case unaryBuiltIns::isNil  : ec.returnedValue = (top == globals.nilObject) ? globals.trueObject : globals.falseObject; break;
        case unaryBuiltIns::notNil : ec.returnedValue = (top != globals.nilObject) ? globals.trueObject : globals.falseObject; break;

        default:
            std::fprintf(stderr, "VM: Invalid opcode %d passed to sendUnary\n", ec.instruction.low);
            std::exit(1);
    }

    ec.stackPush( ec.returnedValue );

    m_messagesSent++;
}

void SmalltalkVM::doSendBinary(TVMExecutionContext& ec)
{
    // Loading the operand objects
    TObject* rightObject = ec.stackPop();
    TObject* leftObject  = ec.stackPop();

    // If operands are both small integers, we may handle it ourselves
    if (isSmallInteger(leftObject) && isSmallInteger(rightObject)) {
        // Loading actual operand values
        int32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
        int32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
        bool unusedCondition;

        // Performing an operation
        switch ( static_cast<binaryBuiltIns::Operator>(ec.instruction.low) ) {
            case binaryBuiltIns::operatorLess:
                ec.returnedValue = (leftOperand < rightOperand) ? globals.trueObject : globals.falseObject;
                break;

            case binaryBuiltIns::operatorLessOrEq:
                ec.returnedValue = (leftOperand <= rightOperand) ? globals.trueObject : globals.falseObject;
                break;

            case binaryBuiltIns::operatorPlus:
                ec.returnedValue = callSmallIntPrimitive(primitive::smallIntAdd, leftOperand, rightOperand, unusedCondition);
                break;

            default:
                std::fprintf(stderr, "VM: Invalid opcode %d passed to sendBinary\n", ec.instruction.low);
                std::exit(1);
        }

        ec.stackPush( ec.returnedValue );
        m_messagesSent++;
    } else {
        // This binary operator is performed on an ordinary object.
        // We do not know how to handle it, thus send the message to the receiver

        // Protecting pointers in case if GC occurs
        hptr<TObject> pRightObject = newPointer(rightObject);
        hptr<TObject> pLeftObject  = newPointer(leftObject);

        hptr<TObjectArray> messageArguments = newObject<TObjectArray>(2/*, false*/);
        messageArguments[1] = pRightObject;
        messageArguments[0] = pLeftObject;

        TSymbol* messageSelector = static_cast<TSymbol*>( globals.binaryMessages[ec.instruction.low] );
        doSendMessage(ec, messageSelector, messageArguments);
    }
}

SmalltalkVM::TExecuteResult SmalltalkVM::doSpecial(hptr<TProcess>& process, TVMExecutionContext& ec)
{
    TByteObject&  byteCodes  = * ec.currentContext->method->byteCodes;
    TObjectArray& arguments  = * ec.currentContext->arguments;
    TSymbolArray& literals   = * ec.currentContext->method->literals;

    switch(ec.instruction.low)
    {
        case special::selfReturn: {
            ec.returnedValue  = arguments[0]; // arguments[0] always keep self
            ec.currentContext = ec.currentContext->previousContext;

            if (ec.currentContext.rawptr() == globals.nilObject) {
                process->context = ec.currentContext;
                process->result  = ec.returnedValue;
                return returnReturned;
            }

            ec.loadPointers();
            ec.stackPush( ec.returnedValue );
        } break;

        case special::stackReturn: {
            ec.returnedValue  = ec.stackPop();
            ec.currentContext = ec.currentContext->previousContext;

            if (ec.currentContext.rawptr() == globals.nilObject) {
                process->context = ec.currentContext;
                process->result  = ec.returnedValue;
                return returnReturned;
            }

            ec.loadPointers();
            ec.stackPush( ec.returnedValue );
        } break;

        case special::blockReturn: {
            ec.returnedValue = ec.stackPop();
            TBlock* contextAsBlock = ec.currentContext.cast<TBlock>();
            ec.currentContext = contextAsBlock->creatingContext->previousContext;

            if (ec.currentContext.rawptr() == globals.nilObject) {
                process->context = ec.currentContext;
                process->result  = ec.returnedValue;
                return returnReturned;
            }

            ec.loadPointers();
            ec.stackPush( ec.returnedValue );
        } break;

        case special::duplicate: {
            // Duplicate an object on the stack
            TObject* copy = ec.stackLast();
            ec.stackPush(copy);
        } break;

        case special::popTop:
            ec.stackPop();
            break;

        case special::branch:
            ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            break;

        case special::branchIfTrue: {
            ec.returnedValue = ec.stackPop();

            if (ec.returnedValue == globals.trueObject)
                ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            else
                ec.bytePointer += 2;
        } break;

        case special::branchIfFalse: {
            ec.returnedValue = ec.stackPop();

            if (ec.returnedValue == globals.falseObject)
                ec.bytePointer = byteCodes[ec.bytePointer] | (byteCodes[ec.bytePointer+1] << 8);
            else
                ec.bytePointer += 2;
        } break;

        case special::sendToSuper: {
            uint32_t literalIndex    = byteCodes[ec.bytePointer++];
            TSymbol* messageSelector = literals[literalIndex];
            TClass*  receiverClass   = ec.currentContext->method->klass->parentClass;
            TObjectArray* messageArguments = ec.stackPop<TObjectArray>();

            doSendMessage(ec, messageSelector, messageArguments, receiverClass);
        } break;
    }

    return returnNoReturn;
}

void SmalltalkVM::doPushConstant(TVMExecutionContext& ec)
{
    uint8_t constant = ec.instruction.low;

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
        case 9: {
            TObject* constInt = reinterpret_cast<TObject*>(newInteger(constant));
            ec.stackPush(constInt);
        } break;

        case pushConstants::nil:         ec.stackPush( globals.nilObject );   break;
        case pushConstants::trueObject:  ec.stackPush( globals.trueObject );  break;
        case pushConstants::falseObject: ec.stackPush( globals.falseObject ); break;
        default:
            std::fprintf(stderr, "VM: unknown push constant %d\n", constant);
            std::exit(1);
    }
}

SmalltalkVM::TExecuteResult SmalltalkVM::doPrimitive(hptr<TProcess>& process, TVMExecutionContext& ec)
{
    uint8_t opcode = (*ec.currentContext->method->byteCodes)[ec.bytePointer++];

    // First of all, executing the primitive
    // If primitive succeeds then stop execution of the current method
    //   and push the result onto the stack of the previous context
    //
    // If primitive call fails, the execution flow continues in the current method after the primitive call.
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
        ec.stackPush( globals.nilObject );
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
            ec.stackPush( ec.returnedValue );

            // Save the stack pointer
            ec.currentContext->stackTop = newInteger(ec.stackTop);

            ec.bytePointer = getIntegerValue(ec.currentContext->bytePointer);
    }

    return returnNoReturn;
}

// TODO Refactor code to make this clean
extern "C" { TObject* sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass, uint32_t callSiteOffset = 0); }

TObject* SmalltalkVM::performPrimitive(uint8_t opcode, hptr<TProcess>& process, TVMExecutionContext& ec, bool& failed) {
    switch (opcode) {
        // FIXME opcodes 253-255 are not standard
        case 255:
            // Debug trap
            std::printf("Debug trap\n");
            break;

        case 254:
            m_memoryManager->collectGarbage();
            break;

#if defined(LLVM)
        case primitive::LLVMsendMessage: { //252
            TObjectArray* args = ec.stackPop<TObjectArray>();
            TSymbol*  selector = ec.stackPop<TSymbol>();
            try {
                return sendMessage(ec.currentContext, selector, args, 0);
            } catch(TBlockReturn& blockReturn) {
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
#endif

        case 247: {
            // Extracting current method info
            TMethod* currentMethod   = ec.currentContext->method;
            TSymbol* currentSelector = currentMethod->name;
            TClass*  currentClass    = currentMethod->klass;

            // Searching for native methods of currentClass
            TNativeMethodMap::iterator iMethods = m_nativeMethods.find(currentClass);
            if (iMethods == m_nativeMethods.end()) {
                failed = true;
                return globals.nilObject;
            }

            // Searching for actual native method by selector
            TSymbolToNativeMethodMap& methodMap = iMethods->second;
            TSymbolToNativeMethodMap::iterator iNativeMethod = methodMap.find(currentSelector);
            if (iNativeMethod == methodMap.end()) {
                failed = true;
                return globals.nilObject;
            }

            // Invoking native method
            TNativeMethodBase* nativeMethod = iNativeMethod->second;
            TObjectArray* arguments = ec.currentContext->arguments;
            TObject* self = arguments->getField(0);

            // And here goes the black magic
            switch (nativeMethod->getType()) {
                case TNativeMethodBase::mtNoArg: {
                    TNativeMethod* pNativeMethod = static_cast<TNativeMethod*>(nativeMethod);
                    return (self ->* pNativeMethod->get()) ();
                }

                case TNativeMethodBase::mtOneArg: {
                    TNativeMethod1* pNativeMethod = static_cast<TNativeMethod1*>(nativeMethod);
                    return (self ->* pNativeMethod->get()) (arguments->getField(1));
                }

                case TNativeMethodBase::mtTwoArg: {
                    TNativeMethod2* pNativeMethod = static_cast<TNativeMethod2*>(nativeMethod);
                    return (self ->* pNativeMethod->get()) (arguments->getField(1), arguments->getField(2));
                }

                case TNativeMethodBase::mtArgArray: {
                    TNativeMethodA* pNativeMethod = static_cast<TNativeMethodA*>(nativeMethod);
                    return (self ->* pNativeMethod->get()) (arguments);
                }
            }
        } break;

        case primitive::startNewProcess: { // 6
            TInteger  value = reinterpret_cast<TInteger>( ec.stackPop() );
            uint32_t  ticks = getIntegerValue(value);
            TProcess* newProcess = ec.stackPop<TProcess>();

            // FIXME possible stack overflow due to recursive call
            TExecuteResult result = this->execute(newProcess, ticks);

            return reinterpret_cast<TObject*>(newInteger(result));
        } break;

        case primitive::allocateObject: { // 7
            // Taking object's size and class from the stack
            TObject* size  = ec.stackPop();
            TClass*  klass = ec.stackPop<TClass>();
            uint32_t fieldsCount = getIntegerValue(reinterpret_cast<TInteger>(size));

            // Instantinating the object. Each object has size and class fields

            return newOrdinaryObject(klass, sizeof(TObject) + fieldsCount * sizeof(TObject*));
        } break;

        case primitive::blockInvoke: { // 8
            TBlock*  block = ec.stackPop<TBlock>();
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
                (*blockTemps)[argumentLocation + index] = ec.stackPop();

            // Switching execution context to the invoking block
            block->previousContext = ec.currentContext->previousContext;
            ec.currentContext = static_cast<TContext*>(block);
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
            int32_t dataSize = getIntegerValue(reinterpret_cast<TInteger>( ec.stackPop() ));
            TClass* klass    = ec.stackPop<TClass>();

            return newBinaryObject(klass, dataSize);
        } break;

        case primitive::arrayAt:      // 24
        case primitive::arrayAtPut: { // 5
            TObject* indexObject = ec.stackPop();
            TObjectArray* array  = ec.stackPop<TObjectArray>();
            TObject* valueObject = 0;

            // If the method is Array:at:put then pop a value from the stack
            if (opcode == primitive::arrayAtPut)
                valueObject = ec.stackPop();

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
                return static_cast<TObject*>(array);
            }
        } break;

        case primitive::cloneByteObject: { // 23
            TClass* klass = ec.stackPop<TClass>();
            hptr<TByteObject> original = newPointer( ec.stackPop<TByteObject>() );

            // Creating clone
            uint32_t dataSize  = original->getSize();
            TByteObject* clone = newBinaryObject(klass, dataSize);

            // Cloning data
            std::memcpy(clone->getBytes(), original->getBytes(), dataSize);
            return static_cast<TObject*>(clone);
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
            TObject* object = ec.stackPop();
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

            TObject* destination            = ec.stackPop();
            TObject* sourceStartOffset      = ec.stackPop();
            TObject* source                 = ec.stackPop();
            TObject* destinationStopOffset  = ec.stackPop();
            TObject* destinationStartOffset = ec.stackPop();

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

        case primitive::ioGetChar:          // 9
        case primitive::ioPutChar:          // 3
        case primitive::ioFileOpen:         // 100
        case primitive::ioFileClose:        // 103
        case primitive::ioFileSetStatIntoArray:   // 105
        case primitive::ioFileReadIntoByteArray:  // 106
        case primitive::ioFileWriteFromByteArray: // 107
        case primitive::ioFileSeek:         // 108

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
            uint32_t argCount = ec.instruction.low;
            hptr<TObjectArray> args = newObject<TObjectArray>(argCount);

            uint32_t i = argCount;
            while (i > 0)
                args[--i] = ec.stackPop();

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

    if (destination->getSize() < static_cast<uint32_t>(iDestinationStopOffset) ||
        source->getSize() < static_cast<uint32_t>(iSourceStartOffset + iCount) )
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
        std::memmove( & destinationBytes[iDestinationStartOffset], & sourceBytes[iSourceStartOffset], iCount );
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
        std::memmove( & destinationFields[iDestinationStartOffset], & sourceFields[iSourceStartOffset], iCount * sizeof(TObject*) );
        return true;
    }

    return false;
}

void SmalltalkVM::printVMStat()
{
    float hitRatio = 100.0 * m_cacheHits / (m_cacheHits + m_cacheMisses);
    std::printf("%d messages sent, cache hits: %d, misses: %d, hit ratio %.2f %%\n",
        m_messagesSent, m_cacheHits, m_cacheMisses, hitRatio);
}

struct TMetaString : public TObject {
    TObject* readline(TString* prompt) {
        checkClass(prompt);

        // TODO Unicode support
        std::string strPrompt(reinterpret_cast<const char*>(prompt->getBytes()), prompt->getSize());

        char* input = ::readline(strPrompt.c_str());
        if (input) {
            uint32_t inputSize = std::strlen(input);

            if (inputSize > 0)
                add_history(input);

            TString* result = static_cast<TString*>( newBinaryObject(globals.stringClass, inputSize) );
            std::memcpy(result->getBytes(), input, inputSize);

            std::free(input);
            return result;
        } else
            return globals.nilObject;
    }

    TObject* test(TObjectArray* args) {
        checkClass(args);
        return globals.nilObject;
    }
};

void SmalltalkVM::registerBuiltinNatives() {
    static const TNativeMethodInfo arrayMethods[] = {
        { "sort:",      NATIVE_METHOD(&TArray<TObject>::sortBy) }
    };

    static const TNativeMethodInfo dictionaryMethods[] = {
        { "at:",        NATIVE_METHOD(&TDictionary::at) }
    };

    static const TNativeMethodInfo metaStringMethods[] = {
        { "readlie:",   NATIVE_METHOD(&TMetaString::readline) },
        { "test:",      NATIVE_METHOD(&TMetaString::test) }
    };

    registerNativeMethods("Dictionary", dictionaryMethods);
    registerNativeMethods("Array", arrayMethods);
    registerNativeMethods("MetaString", metaStringMethods);
}
