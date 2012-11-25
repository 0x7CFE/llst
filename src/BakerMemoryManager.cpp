#include <memory.h>

void* BakerMemoryManager::allocateMemory(size_t requestedSize)
{
    size_t attempts = 3;
    while (attempts-- > 0) {
        size_t occupiedSpace = activeHeapPointer - activeHeapBase;
        
        if (heapSize - occupiedSpace < requestedSize) {
            collectGarbage();
            continue;
        }
        
        void* result = activeHeapPointer;
        activeHeapPointer += requestedSize;
        return result;
    }
    
    return 0;
}