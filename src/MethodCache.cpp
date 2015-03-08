#include <MethodCache.h>
#include <string.h>

TMethod* MethodCache::get(TSymbol* selector, TClass* klass) {
    uint32_t hash = reinterpret_cast<uint32_t>(selector) ^ reinterpret_cast<uint32_t>(klass);
    TMethodCacheEntry& entry = m_cache[hash % LOOKUP_CACHE_SIZE];

    if (entry.selector == selector && entry.klass == klass) {
        m_stat.hits++;
        return entry.method;
    } else {
        m_stat.misses++;
        return 0;
    }
}

void MethodCache::set(TSymbol* selector, TClass* klass, TMethod* method) {
    uint32_t hash = reinterpret_cast<uint32_t>(selector) ^ reinterpret_cast<uint32_t>(klass);
    TMethodCacheEntry& entry = m_cache[hash % LOOKUP_CACHE_SIZE];

    entry.selector = selector;
    entry.klass    = klass;
    entry.method   = method;
}

void MethodCache::clear() {
    memset(m_cache, 0, sizeof(m_cache));
}

MethodCache::Stat MethodCache::getStat() const {
    return m_stat;
}
