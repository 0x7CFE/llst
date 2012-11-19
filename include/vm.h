#include "types.h"
#include <list>

template<class T> T* newObject(size_t objectSize = 0);
    
class SmalltalkVM {
public:
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
    
    TClass* getRootClass(TClassID id);
    
    enum TExecuteResult {
        returnError = 2,
        returnBadMethod,
        returnReturned,
        returnTimeExpired,
        returnBreak
    }; 
    
    std::list<TObject*> rootStack;
    
    struct TMethodCacheEntry
    {
        TObject* methodName;
        TClass*  receiverClass;
        TMethod* method;
    };
    
    const int LOOKUP_CACHE_SIZE = 4096;
    TMethodCacheEntry m_lookupCache[LOOKUP_CACHE_SIZE];
    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;

    uint32_t getIntegerValue(TInteger value) { return (uint32_t) value >> 1; }
    TInteger newInteger(uint32_t value) { return (value << 1) | 1; }

    // lexicographic comparison of two byte objects
    int compareSymbols(const TByteObject* left, const TByteObject* right);
    
    // locate the method in the hierarchy of the class
    TMethod* lookupMethod(const TObject* selector, const TObject* klass);
    
    // fast method lookup in the method cache
    TMethod* lookupMethodInCache(const TObject* selector, const TObject* klass);
    
    // flush the method lookup cache
    void flushCache();
    
    int execute(TProcess* process, uint32_t ticks);
    void doPushConstant(uint8_t constant, TArray* stack, uint32_t& stackTop);
    void doSendMessage(TContext* context, TMethod* method);
public:
    
};

