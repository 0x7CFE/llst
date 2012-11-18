#include <vm.h>
#include <string.h>

int SmalltalkVM::compareSymbols(const TByteObject* left, const TByteObject* right)
{
    // This function compares two byte objects depending on their lenght and contents
   
    const uint32_t leftSize = left->getSize();
    const uint32_t rightSize = right->getSize();
    uint32_t minSize = leftSize;
    
    if (rightSize < minSize)
        minSize = rightSize;
    
    return memcmp(left->getBytes(), right->getBytes(), minSize);
    
//     // Comparing the byte string symbol by symbol
//     for (uint32_t i = 0; i < minSize; i++)
//     {
//         if (left[i] != right[i])
//             return left[i] - right[i];
//     }
//     
//     return leftSize - rightSize;
}

TObject* SmalltalkVM::lookupMethod(const TObject* selector, const TClass* klass)
{
    //Scanning through the class hierarchy from the klass up to the Object
    for (; klass != m_globals.nilObject; klass = klass->parentClass)
    {
        
    }
}
