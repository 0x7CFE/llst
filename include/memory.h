#ifndef LLST_MEMORY_H_INCLUDED
#define LLST_MEMORY_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

class IMemoryAllocator {
public:
    virtual void*  allocateMemory(size_t size) = 0;
    virtual void   releaseMemory(void* address) = 0;
    virtual size_t allocatedSize() = 0;
};

class HeapMemoryManager : public IMemoryAllocator {
private:
    uint8_t*  heapBase;
    size_t    heapSize;
    
    uint8_t*  heapPointer;
public:
    HeapMemoryManager(size_t heapSize);
    virtual ~HeapMemoryManager();
    
    virtual void*  allocateMemory(size_t size);
    virtual void   releaseMemory(void* address);
    virtual size_t allocatedSize() { return (size_t)heapPointer - (size_t)heapBase; }
};


#endif