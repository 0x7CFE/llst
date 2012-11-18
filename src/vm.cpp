#include <vm.h>

SmalltalkVM::compareSymbols(const TByteObject* left, const TByteObject* right)
{
    uint32_t leftSize = left->getSize();
    uint32_t rightSize = right->getSize();
    uint32_t minSize = leftSize;
    
    if (rightSize < minSize)
        minSize = rightSize;
    
    for (uint32_t i = 0; i < minSize; i++)
    {
        if (left[i] != right[i])
            return left[i] - right[i];
    }
    
    return leftSize - rightSize;
}

