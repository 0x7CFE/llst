#include <types.h>
#include <string.h>

int TDictionary::compareSymbols(TSymbol* left, TSymbol* right)
{
    // This function compares two byte objects depending on their lenght and contents
   
    uint32_t leftSize  = left->getSize();
    uint32_t rightSize = right->getSize();
    uint32_t minSize   = (leftSize < rightSize) ? leftSize : rightSize;
    
    // Comparing the byte strings byte by byte
    int result = memcmp(left->getBytes(), right->getBytes(), minSize);
    
    // If bytestrings are equal, checking whichever is longer
    if (result)
        return result;
    else
        return leftSize - rightSize;
}

int TDictionary::compareSymbols(TSymbol* left, const char* right)
{
    // This function compares byte object and 
    // null terminated string depending on their lenght and contents
    
    uint32_t leftSize   = left->getSize();
    uint32_t rightSize  = strlen(right);
    uint32_t minSize    = (leftSize < rightSize) ? leftSize : rightSize;
    
    uint8_t* leftBytes  = left->getBytes();
    uint8_t* rightBytes = (uint8_t*) right;
    
    // Comparing the byte strings byte by byte
    int result = memcmp(leftBytes, rightBytes, minSize);

    // If bytestrings are equal, checking whichever is longer
    if (result)
        return result;
    else
        return leftSize - rightSize;
}

TObject* TDictionary::find(TSymbol* key)
{
    TSymbolArray& keys   = * this->keys;
    TObjectArray& values = * this->values;
    
    // keys are stored in order
    // thus we may apply binary search
    
    uint32_t low  = 0;
    uint32_t high = keys.getSize();
    
    while (low < high) {
        uint32_t mid = (low + high) / 2;
        TSymbol* candidate = (TSymbol*) keys[mid];
        
        // Each symbol is unique within the whole image
        // This allows us to compare pointers instead of contents
        if (candidate == key)
            return values[mid];
        
        if (compareSymbols(candidate, key) < 0)
            low = mid + 1;
        else 
            high = mid;
    }
    
    return 0;
}

TObject* TDictionary::find(const char* key)
{
    TSymbolArray& keys   = * this->keys;
    TObjectArray& values = * this->values;
    
    // Keys are stored in order
    // Thus we may apply binary search
    
    uint32_t low  = 0;
    uint32_t high = keys.getSize();
    
    while (low < high) {
        uint32_t mid = (low + high) / 2;
        TSymbol* candidate = (TSymbol*) keys[mid];
        
        int comparison = compareSymbols(candidate, key);
        
        if (comparison < 0)
            low = mid + 1;
        else if (comparison > 0)
            high = mid;
        else
            return values[mid];
    }
    
    return 0;
}
