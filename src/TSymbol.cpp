#include <types.h>
#include <string.h>
#include <algorithm>

bool TSymbol::less::operator() (const TSymbol* left, const TSymbol* right) const
{
    const uint8_t* leftBase = left->getBytes();
    const uint8_t* leftEnd  = leftBase + left->getSize();
    
    const uint8_t* rightBase = right->getBytes();
    const uint8_t* rightEnd  = rightBase + right->getSize();
    
    return std::lexicographical_compare(leftBase, leftEnd, rightBase, rightEnd);
}

bool TSymbol::less::operator() (const TSymbol* left, const char* right) const
{
    const uint8_t* leftBase = left->getBytes();
    const uint8_t* leftEnd  = leftBase + left->getSize();
    
    return std::lexicographical_compare(leftBase, leftEnd, right, right + strlen(right));
}

bool TSymbol::less::operator() (const char* left, const TSymbol* right) const
{
    const uint8_t* rightBase = right->getBytes();
    const uint8_t* rightEnd  = rightBase + right->getSize();
    
    return std::lexicographical_compare(left, left + strlen(left), rightBase, rightEnd);
}
