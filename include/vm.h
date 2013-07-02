/*
 *    vm.h
 *
 *    LLST virtual machine related classes and structures
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

#ifndef LLST_VM_H_INCLUDED
#define LLST_VM_H_INCLUDED

#include <list>

#include <types.h>
#include <memory.h>
#include <stdlib.h>
#include <opcodes.h>
#include <stdio.h>
#include <TInstruction.h>

class SmalltalkVM {
public:
    enum TExecuteResult {
        returnError = 2,
        returnBadMethod,
        returnReturned,
        returnTimeExpired,
        returnBreak,

        returnNoReturn = 255
    };
private:
    struct TVMExecutionContext {
    private:
        // TODO Think about proper memory organization
        // TODO Implement stackPush / stackPop / reloadContextData etc ?
        IMemoryManager* memoryManager;
    public:
        hptr<TContext> currentContext;

        TInstruction   instruction;
        uint32_t       bytePointer;
        uint32_t       stackTop;

        hptr<TObject>  returnedValue;

        void loadPointers() {
            bytePointer = getIntegerValue(currentContext->bytePointer);
            stackTop    = getIntegerValue(currentContext->stackTop);
        }

        void storePointers() {
            currentContext->bytePointer = newInteger(bytePointer);
            currentContext->stackTop    = newInteger(stackTop);
        }

        TVMExecutionContext(IMemoryManager* mm) :
            memoryManager(mm),
            currentContext((TContext*) globals.nilObject, mm),
            returnedValue(globals.nilObject, mm)
        { }
    };

    struct TMethodCacheEntry
    {
        TObject* methodName;
        TClass*  receiverClass;
        TMethod* method;
    };

    static const unsigned int LOOKUP_CACHE_SIZE = 512;
    TMethodCacheEntry m_lookupCache[LOOKUP_CACHE_SIZE];
    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;
    uint32_t m_messagesSent;


    // fast method lookup in the method cache
    TMethod* lookupMethodInCache(TSymbol* selector, TClass* klass);
public:
    TMethod* lookupMethod(TSymbol* selector, TClass* klass);

    bool checkRoot(TObject* value, TObject** objectSlot);
private:

    void updateMethodCache(TSymbol* selector, TClass* klass, TMethod* method);

    // flush the method lookup cache
    void flushMethodCache();

    void doPushConstant(TVMExecutionContext& ec);
    void doPushBlock(TVMExecutionContext& ec);
    void doMarkArguments(TVMExecutionContext& ec);
    //Takes selector, arguments from context and sends message
    //The class is taken from the first argument
    void doSendMessage(TVMExecutionContext& ec); 
    //This method is used to send message to the first argument
    //If receiverClass != 0 then the class is not taken from the first argument (implementation of sendToSuper)
    void doSendMessage(TVMExecutionContext& ec, TSymbol* selector, TObjectArray* arguments, TClass* receiverClass = 0);
    void doSendUnary(TVMExecutionContext& ec);
    void doSendBinary(TVMExecutionContext& ec);

    TObject* performPrimitive(uint8_t opcode, hptr<TProcess>& process, TVMExecutionContext& ec, bool& failed);
    TExecuteResult doPrimitive(hptr<TProcess>& process, TVMExecutionContext& ec);
    TExecuteResult doSpecial  (hptr<TProcess>& process, TVMExecutionContext& ec);


    Image*          m_image;
    IMemoryManager* m_memoryManager;

    bool m_lastGCOccured;
    void onCollectionOccured();

//    std::list< hptr<TProcess> > rootStack;
    std::list< TObject* > rootStack;
public:
    bool doBulkReplace( TObject* destination, TObject* destinationStartOffset, TObject* destinationStopOffset, TObject* source, TObject* sourceStartOffset);
    // The result may be nil if the opcode execution fails (division by zero etc)
    TObject* doSmallInt(primitive::SmallIntOpcode opcode, int32_t leftOperand, int32_t rightOperand);

    // NOTE For typical operation these should not be used directly.
    //      Use the template newObject<T>() instead
    TByteObject* newBinaryObject  (TClass* klass, size_t dataSize);
    TObject*     newOrdinaryObject(TClass* klass, size_t slotSize);

    void pushProcess(TProcess* process) {
        //rootStack.push_back(newPointer(process));
        rootStack.push_back(process);
        m_memoryManager->registerExternalPointer(& rootStack.back());
    }

    TProcess* popProcess() {
        m_memoryManager->releaseExternalPointer(& rootStack.back());
        TProcess* process = (TProcess*) rootStack.back();
        rootStack.pop_back();
        return process;
    }

    SmalltalkVM(Image* image, IMemoryManager* memoryManager)
        : m_cacheHits(0), m_cacheMisses(0), m_messagesSent(0), m_image(image),
        m_memoryManager(memoryManager), m_lastGCOccured(false) //, ec(memoryManager)
    {
        flushMethodCache();
    }

    TExecuteResult execute(TProcess* p, uint32_t ticks);
    template<class T> hptr<T> newObject(size_t dataSize = 0, bool registerPointer = true);
    template<class T> hptr<T> newPointer(T* object) { return hptr<T>(object, m_memoryManager); }

    void printVMStat();
};

template<class T> hptr<T> SmalltalkVM::newObject(size_t dataSize /*= 0*/, bool registerPointer /*= true*/)
{
    // TODO fast access to common classes
    TClass* klass = (TClass*) m_image->getGlobal(T::InstanceClassName());
    if (!klass)
        return hptr<T>((T*) globals.nilObject, m_memoryManager);
    

    if (T::InstancesAreBinary()) {
        return hptr<T>((T*) newBinaryObject(klass, dataSize), m_memoryManager, registerPointer);
    } else {
        size_t slotSize = sizeof(T) + dataSize * sizeof(T*);
        return hptr<T>((T*) newOrdinaryObject(klass, slotSize), m_memoryManager, registerPointer);
    }
}

// Specializations of newObject for known types
template<> hptr<TObjectArray> SmalltalkVM::newObject<TObjectArray>(size_t dataSize, bool registerPointer);
template<> hptr<TSymbolArray> SmalltalkVM::newObject<TSymbolArray>(size_t dataSize, bool registerPointer);
template<> hptr<TContext> SmalltalkVM::newObject<TContext>(size_t dataSize, bool registerPointer);
template<> hptr<TBlock> SmalltalkVM::newObject<TBlock>(size_t dataSize, bool registerPointer);


#endif
