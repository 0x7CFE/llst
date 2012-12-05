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
    enum Opcode {
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
        doSpecial = 15
    };
    
    enum Special {
        selfReturn = 1,
        stackReturn,
        blockReturn,
        duplicate,
        popTop,
        branch,
        branchIfTrue,
        branchIfFalse,
        sendToSuper = 11,
        breakpoint = 12
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
    
    enum SmallIntOpcode {
        smallIntAdd = 10,
        smallIntDiv,
        smallIntMod,
        smallIntLess,
        smallIntEqual,
        smallIntMul,
        smallIntSub,
        smallIntBitOr = 36,
        smallIntBitAnd = 37,
        smallIntBitShift = 39
    };
    
    enum {
        ioGetChar = 9,
        ioPutChar = 3
    };
    
    enum {
        stringAt        = 21,
        stringAtPut     = 22,
        arrayAt         = 24,
        arrayAtPut      = 5
    };
    
    enum IntegerOpcode {
        integerDiv = 25,
        integerMod,
        integerAdd,
        integerMul,
        integerSub,
        integerLess,
        integerEqual,
    };
    
    enum {
        returnIsEqual     = 1,
        returnClass       = 2,
        returnSize        = 4,
        inAtPut           = 5,
        allocateObject    = 7,
        blockInvoke       = 8,
        allocateByteArray = 20,
        cloneByteObject   = 23
    };
    
    struct TVMExecutionContext {
        TContext*    currentContext;
        
        TInstruction instruction;
        uint32_t     bytePointer;
        uint32_t     stackTop;
        
        // NOTE these will be invalidated during the GC
        TObject*     returnedValue;
        TClass*      lastReceiver;
        
        void loadPointers() {
            bytePointer = getIntegerValue(currentContext->bytePointer);
            stackTop    = getIntegerValue(currentContext->stackTop);
        }
        
        void storePointers() {
            currentContext->bytePointer = newInteger(bytePointer);
            currentContext->stackTop    = newInteger(stackTop);
        }
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
    void flushMethodCache();
    
    void doPushConstant(uint8_t constant, TVMExecutionContext& ec);
    void doPushBlock(TVMExecutionContext& ec);
    void doMarkArguments(TVMExecutionContext& ec);
    void doSendMessage(TVMExecutionContext& ec);
    void doSendUnary(TVMExecutionContext& ec);
    void doSendBinary(TVMExecutionContext& ec);
    
    TObject* doExecutePrimitive(uint8_t opcode, TProcess& process, TVMExecutionContext& ec);
    
    TExecuteResult doDoSpecial(TProcess*& process, TVMExecutionContext& ec);
    
    // The result may be nil if the opcode execution fails (division by zero etc)
    TObject* doSmallInt(
        SmallIntOpcode opcode,
        uint32_t leftOperand,
        uint32_t rightOperand);
        
    void failPrimitive(
        TObjectArray& stack,
        uint32_t& stackTop);
    
    // TODO Think about other memory organization
    std::vector<TObject*> m_rootStack;
    
    Image*          m_image;
    IMemoryManager* m_memoryManager;
    
    bool m_lastGCOccured;
    void onCollectionOccured();
    
    TObject* newBinaryObject(TClass* klass, size_t dataSize);
    TObject* newOrdinaryObject(TClass* klass, size_t slotSize);
    
    // Helper functions for backTraceContext()
    void printByteObject(TByteObject* value);
    void printValue(uint32_t index, TObject* value, TObject* previousValue = 0);
    void printContents(TObjectArray& array);
    void backTraceContext(TContext* context);
    
    // Name without m_ to be short because it is widely used
    TVMExecutionContext ec;
    
    static SmalltalkVM* s_instance;
public:
//    void Initialize() { s_instance = new SmalltalkVM(); }
    SmalltalkVM* GetInstance() { return s_instance; }
    
    SmalltalkVM(Image* image, IMemoryManager* memoryManager) 
        : m_cacheHits(0), m_cacheMisses(0), m_image(image), 
        m_memoryManager(memoryManager), m_lastGCOccured(false) 
    {}
    
    TExecuteResult execute(TProcess* process, uint32_t ticks);
    template<class T> hptr<T> newObject(size_t dataSize = 0);
    template<class T> hptr<T> newPointer(T* object) { return hptr<T>(object, m_memoryManager, true); }
};

template<class T> hptr<T> SmalltalkVM::newObject(size_t dataSize /*= 0*/)
{
    // TODO fast access to common classes
    TClass* klass = (TClass*) m_image->getGlobal(T::InstanceClassName());
    if (!klass)
        return hptr<T>((T*) globals.nilObject, m_memoryManager, true);
    
    if (T::InstancesAreBinary()) {   
        return hptr<T>((T*) newBinaryObject(klass, dataSize), m_memoryManager, true);
        // return (T*) newBinaryObject(klass, dataSize);
    } else {
        size_t slotSize = sizeof(T) + dataSize * sizeof(T*);
        return hptr<T>((T*) newOrdinaryObject(klass, slotSize), m_memoryManager, true);
        //return (T*) newOrdinaryObject(klass, slotSize);
    }
}

// Specializations of newObject for known types
// template<> hptr<TObjectArray> SmalltalkVM::newObject<TObjectArray>(size_t dataSize /*= 0*/);
// template<> hptr<TContext> SmalltalkVM::newObject<TContext>(size_t dataSize /*= 0*/);
// template<> hptr<TBlock> SmalltalkVM::newObject<TBlock>(size_t dataSize /*= 0*/);


#endif
