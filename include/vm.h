#include "types.h"
#include <list>


// struct TRootStack {
//     TObject* rootStack[];
//     uint32_t rootTop;
// };

struct TLookupCacheEntry
{
    TObject* name;
    TClass*  klass;
    TMethod* method;
};


template<class T> T* newObject(size_t objectSize = 0);
    
class SmalltalkVM {
public:
private:
    const int LOOKUP_CACHE_SIZE = 4096;
    TLookupCacheEntry m_lookupCache[LOOKUP_CACHE_SIZE];
    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;
    std::list<TObject*> rootStack;

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
    
    // lexicographic comparison of two byte objects
    int compareSymbols(const TByteObject* left, const TByteObject* right);
    
    // locate the method in the hierarchy of the class
    TMethod* lookupMethod(const TObject* selector, const TObject* klass);
    
    // flush the method lookup cache
    void flushCache();
    
    uint32_t getIntegerValue(TInteger value) { return (uint32_t) value >> 1; }
    TInteger newInteger(uint32_t value) { return (value << 1) | 1; }

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
    
    int execute(TProcess* process, uint32_t ticks);
public:
    
};

