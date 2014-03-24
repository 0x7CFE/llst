/*
 *    vm.h
 *
 *    LLST virtual machine related classes and structures
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

#ifndef LLST_VM_H_INCLUDED
#define LLST_VM_H_INCLUDED

#include <list>

#include <types.h>
#include <memory.h>
#include <instructions.h>

template <int I>
struct Int2Type
{
    enum { value = I };
    /* The int-to-type idiom helps to use pattern matching in C++.
     * C++ allows you to overload functions only by type, not by value,
     * Int2Type creates a new struct per each integral constant, which allows to overload functions.
     *
     * Example:
     * int factorial(Int2Type<0>) { return 1; }
     * template<int n> int factorial(Int2Type<n>) { return n*factorial(Int2Type<n-1>()); }
     */
};

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
        // TODO Implement reloadContextData etc ?
        SmalltalkVM* m_vm;
    public:
        hptr<TContext> currentContext;

        st::TSmalltalkInstruction instruction;
        uint16_t       bytePointer;
        uint32_t       stackTop;

        hptr<TObject>  returnedValue;

        void loadPointers() {
            bytePointer = currentContext->bytePointer;
            stackTop    = currentContext->stackTop;
        }

        void storePointers() {
            currentContext->bytePointer = bytePointer;
            currentContext->stackTop    = stackTop;
        }

        void stackPush(TObject* object);

        TObject* stackLast() {
            return currentContext->stack->getField(stackTop - 1);
        }

        TObject* stackPop() {
            TObject* top = currentContext->stack->getField(--stackTop);
            return top;
        }
        template <typename ResultType>
        ResultType* stackPop() {
            return static_cast<ResultType*>( stackPop() );
        }

        TVMExecutionContext(IMemoryManager* mm, SmalltalkVM* vm) :
            m_vm(vm),
            currentContext( static_cast<TContext*>(globals.nilObject), mm),
            instruction(opcode::extended),
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

public:
    bool doBulkReplace( TObject* destination, TObject* destinationStartOffset, TObject* destinationStopOffset, TObject* source, TObject* sourceStartOffset);
    //This function is used to lookup and return method for #doesNotUnderstand for a given selector of a given object with appropriate arguments.
    void setupVarsForDoesNotUnderstand(/*out*/ hptr<TMethod>& method,/*out*/ hptr<TObjectArray>& arguments, TSymbol* selector, TClass* receiverClass);

    // NOTE For typical operation these should not be used directly.
    //      Use the template newObject<T>() instead
    TByteObject* newBinaryObject  (TClass* klass, std::size_t dataSize);
    TObject*     newOrdinaryObject(TClass* klass, std::size_t slotSize);

    SmalltalkVM(Image* image, IMemoryManager* memoryManager)
        : m_cacheHits(0), m_cacheMisses(0), m_messagesSent(0), m_image(image),
        m_memoryManager(memoryManager), m_lastGCOccured(false) //, ec(memoryManager)
    {
        flushMethodCache();
    }

    TExecuteResult execute(TProcess* p, uint32_t ticks);
    template<class T> hptr<T> newObject(std::size_t dataSize = 0, bool registerPointer = true);

    template<class T> hptr<T> newObjectWrapper(/*InstancesAreBinary*/ Int2Type<false>, std::size_t dataSize = 0, bool registerPointer = true);
    template<class T> hptr<T> newObjectWrapper(/*InstancesAreBinary*/ Int2Type<true> , std::size_t dataSize = 0, bool registerPointer = true);

    template<class T> hptr<T> newPointer(T* object) { return hptr<T>(object, m_memoryManager); }

    void printVMStat();
};

template<class T> hptr<T> SmalltalkVM::newObject(std::size_t dataSize /*= 0*/, bool registerPointer /*= true*/)
{
    /* There are two types of objects. Their fields are treated as:
     * 1) objects (ordinary objects)
     * 2) bytes (binary objects)
     * The instaces of ordinary and binary objects are created in a different way.
     * We have to choose which kind of object we are going to create: ordinary or binary.
     * Each class T contains enum field InstancesAreBinary, which keeps the info.
     * If class T derives from TByteObject and InstancesAreBinary==false you get compile error and vice versa.
     */
    return newObjectWrapper<T>(Int2Type<T::InstancesAreBinary>(), dataSize, registerPointer);
}

template<class T> hptr<T> SmalltalkVM::newObjectWrapper(/*InstancesAreBinary*/ Int2Type<false>, std::size_t dataSize /*= 0*/, bool registerPointer /*= true*/)
{
    TClass* klass = m_image->getGlobal<TClass>(T::InstanceClassName());
    if (!klass)
        return hptr<T>( static_cast<T*>(globals.nilObject), m_memoryManager);

    std::size_t slotSize = sizeof(T) + dataSize * sizeof(T*);
    return hptr<T>( static_cast<T*>( newOrdinaryObject(klass, slotSize) ), m_memoryManager, registerPointer );
}

template<class T> hptr<T> SmalltalkVM::newObjectWrapper(/*InstancesAreBinary*/ Int2Type<true>,  std::size_t dataSize /*= 0*/, bool registerPointer /*= true*/)
{
    TClass* klass = m_image->getGlobal<TClass>(T::InstanceClassName());
    if (!klass)
        return hptr<T>( static_cast<T*>(globals.nilObject), m_memoryManager );

    return hptr<T>( static_cast<T*>( newBinaryObject(klass, dataSize) ), m_memoryManager, registerPointer );
}

// Specializations of newObject for known types
template<> hptr<TObjectArray> SmalltalkVM::newObject<TObjectArray>(std::size_t dataSize, bool registerPointer);
template<> hptr<TSymbolArray> SmalltalkVM::newObject<TSymbolArray>(std::size_t dataSize, bool registerPointer);
template<> hptr<TContext> SmalltalkVM::newObject<TContext>(std::size_t dataSize, bool registerPointer);
template<> hptr<TBlock> SmalltalkVM::newObject<TBlock>(std::size_t dataSize, bool registerPointer);

#endif
