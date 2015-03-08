#ifndef LLST_METHOD_CACHE_H_INCLUDED
#define LLST_METHOD_CACHE_H_INCLUDED

#include <vector>
#include <types.h>

class MethodCache
{
public:
    struct Stat {
        uint32_t hits;
        uint32_t misses;
        float getRatio() const {
            if ( (hits + misses) == 0)
                return 0.0;
            return 100.0 * hits / (hits + misses);
        }
        Stat() : hits(), misses() {}
    };
private:
    struct TMethodCacheEntry
    {
        TSymbol* selector;
        TClass*  klass;
        TMethod* method;
    };
    static const unsigned int LOOKUP_CACHE_SIZE = 512;
    TMethodCacheEntry m_cache[LOOKUP_CACHE_SIZE];
    Stat m_stat;
public:
    MethodCache() : m_cache(), m_stat() {}
    TMethod* get(TSymbol* selector, TClass* klass); // return 0 if not found
    void set(TSymbol* selector, TClass* klass, TMethod* method);
    void clear();
    Stat getStat() const;
};

#endif
