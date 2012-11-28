#ifndef LLST_VM_H_INCLUDED
#define LLST_VM_H_INCLUDED

#include <list>

#include <types.h>
#include <memory.h>
#include <stdlib.h>

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
    enum {
        extended = 0,
        pushInstance,
        pushArgument,    
        pushTemporary,   
        pushLiteral,     
        pushConstant,    
        assignInstance,  
        assignTemporary, 
        markArguments,   
        sendMessage,     
        sendUnary,       
        sendBinary,      
        pushBlock,       
        doPrimitive,     
        doSpecial       
    };
    
    enum Special {
        SelfReturn = 1,
        StackReturn,
        BlockReturn,
        Duplicate,
        PopTop,
        Branch,
        BranchIfTrue,
        BranchIfFalse,
        SendToSuper = 11,
        Breakpoint = 12
    };
    
    enum {
        nilConst = 10,
        trueConst,
        falseConst
    };
    
    enum TClassID {
        Object,
        Class,
        Method,
        Context,
        Process,
        Array,
        Dictionary,
        Block,
    };
    
    struct TMethodCacheEntry
    {
        TObject* methodName;
        TClass*  receiverClass;
        TMethod* method;
    };
    
    static const unsigned int LOOKUP_CACHE_SIZE = 4096;
    TMethodCacheEntry m_lookupCache[LOOKUP_CACHE_SIZE];
    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;

    // lexicographic comparison of two byte objects
//     int compareSymbols(const TByteObject* left, const TByteObject* right);
    
    // locate the method in the hierarchy of the class
    TMethod* lookupMethod(TSymbol* selector, TClass* klass);
    
    // fast method lookup in the method cache
    TMethod* lookupMethodInCache(TSymbol* selector, TClass* klass);
    
    // flush the method lookup cache
    void flushCache();
    
    void doPushConstant(uint8_t constant, TObjectArray& stack, uint32_t& stackTop);
    
    void doSendMessage(
        TSymbol* selector, 
        TObjectArray& arguments, 
        TContext* context, 
        uint32_t& stackTop);
    
    TObject* doExecutePrimitive(
        uint8_t opcode, 
        TObjectArray& stack, 
        uint32_t& stackTop,
        TProcess& process);
    
    TExecuteResult doDoSpecial(
        TInstruction instruction, 
        TContext* context, 
        uint32_t& stackTop,
        TMethod*& method,
        uint32_t& bytePointer,
        TProcess*& process,
        TObject*& returnedValue);
    
    TObject* doSmallInt(
        uint32_t opcode,
        uint32_t lhs,
        uint32_t rhs);
    
    std::list<TObject*> m_rootStack;
    
    Image*          m_image;
    IMemoryManager* m_memoryManager;
    
public:    
    TExecuteResult execute(TProcess* process, uint32_t ticks);
    SmalltalkVM(Image* image, IMemoryManager* memoryManager) 
        : m_image(image), m_memoryManager(m_memoryManager) {}
    
    template<class T> T* newObject(size_t objectSize = 0);
    TObject* newObject(TSymbol* className, size_t objectSize);
    TObject* newObject(TClass* klass);
    
};

template<class T> T* SmalltalkVM::newObject(size_t objectSize /*= 0*/)
{
    // TODO fast access to common classes
    TClass* klass = (TClass*) m_image->getGlobal(T::InstanceClassName());
    if (!klass)
        return (T*) globals.nilObject;
    
    // Slot size is computed depending on the object type
    size_t slotSize = 0;
    if (T::InstancesAreBinary())    
        slotSize = sizeof(T) + objectSize;
    else 
        slotSize = sizeof(T) + objectSize * sizeof(T*);
        
    void* objectSlot = m_memoryManager->allocate(slotSize);
    if (!objectSlot)
        return (T*) globals.nilObject;
    
    T* instance = (T*) new (objectSlot) TObject(objectSize, klass);
    if (! T::InstancesAreBinary())     
    {
        for (uint32_t i = 0; i < objectSize; i++)
            instance->putField(i, globals.nilObject);
    }
    
    return instance;
}

// Specialization of newObject for array
template<> TObjectArray* SmalltalkVM::newObject<TObjectArray>(size_t objectSize /*= 0*/);


#endif
