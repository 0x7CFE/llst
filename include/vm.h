#include "types.h"


struct TRootStack {
    TObject* rootStack[];
    uint32_t rootTop;
};

struct {
    TObject* nilObject;
    TObject* trueObject;
    TObject* falseObject;
    TClass*  smallIntClass;
    TClass*  arrayClass;
    TClass*  blockClass;
    TClass*  contextClass;
    TClass*  stringClass;
    TObject* globalsObject;
    TObject* initialMethod;
    TObject* binaryMessages[3]; // NOTE
    TClass*  integerClass;
    TObject* badMethodSymbol;
} globals;


struct TLookupCacheEntry
{
    TObject* name;
    TClass*  klass;
    TMethod* method;
};

class SmalltalkVM {
private:
    const int LOOKUP_CACHE_SIZE = 4096;
    TLookupCacheEntry m_lookupCache[LOOKUP_CACHE_SIZE];
    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;

    int compareSymbols(const TByteObject* left, const TByteObject* right);
public:
    
};

