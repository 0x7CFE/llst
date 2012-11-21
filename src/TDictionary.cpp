#include <types.h>
#include <string.h>

int TDictionary::compareSymbols(TSymbol* left, TSymbol* right)
{
    // This function compares two byte objects depending on their lenght and contents
   
    uint32_t leftSize = left->getSize();
    uint32_t rightSize = right->getSize();
    uint32_t minSize = (leftSize < rightSize) ? leftSize : rightSize;
    
    // Comparing the byte string symbol by symbol
    int result = memcmp(left->getBytes(), right->getBytes(), minSize);
    
    if (result)
        return result;
    else
        return leftSize - rightSize;
}

int TDictionary::compareSymbols(TSymbol* left, const char* right)
{
    // This function compares byte object and 
    // null terminated string depending on their lenght and contents
    
    uint32_t leftSize = left->getSize();
    uint32_t rightSize = strlen(right);
    uint32_t minSize = (leftSize < rightSize) ? leftSize : rightSize;
    
    uint8_t* leftBytes = left->getBytes();
    uint8_t* rightBytes = (uint8_t*) right;
    
    // Comparing the byte string symbol by symbol
    int result = memcmp(leftBytes, rightBytes, minSize);

    if (result)
        return result;
    else
        return leftSize - rightSize;
}

TObject* TDictionary::find(const TSymbol* key)
{
    TArray* keys   = this->keys;
    TArray* values = this->values;
    
    // keys are stored in order
    // thus we may apply binary search
    
    uint32_t low = 0;
    uint32_t high = keys->getSize();
    
    while (low < high) {
        uint32_t mid = (low + high) / 2;
        TSymbol* candidate = keys[mid];
        
        if (candidate == key)
            return values[mid];
        
        if (compareSymbols(candidate, key) < 0)
            high = mid;
        else 
            low = mid + 1;
    }
    
    return nullptr;
}

TObject* TDictionary::find(const char* key)
{
    TArray* keys   = this->keys;
    TArray* values = this->values;
    
    // keys are stored in order
    // thus we may apply binary search
    
    uint32_t low = 0;
    uint32_t high = keys->getSize();
    
    while (low < high) {
        uint32_t mid = (low + high) / 2;
        TSymbol* candidate = keys[mid];
        
        int comparison = compareSymbols(candidate, key);
        
        if (comparison < 0)
            high = mid;
        else if (comparison > 0)
            low = mid + 1;
        else
            return values[mid];
    }
    
    return nullptr;
}
