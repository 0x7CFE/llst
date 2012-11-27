#ifndef LLST_MEMORY_H_INCLUDED
#define LLST_MEMORY_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <types.h>

class IMemoryAllocator {
public:
    virtual void* allocateMemory(size_t size) = 0;
};

class IMemoryManager {
public:
    virtual void collectGarbage();
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
    virtual size_t allocatedSize() { return (size_t)heapPointer - (size_t)heapBase; }
};


// During GC we need to treat all objects in a very simple manner, 
// just as pointer holders. Class field is also a pointer so we
// treat it just as one more object field
struct TMovableObject {
    TSize size;
    TMovableObject* data[0];
};

// Simple memory manager implementing classic baker two space algorithm.
// Each time two separate heaps are allocated but only one is active.
//
// When we need to allocate more memory but no space left on the current heap
// then garbage collection procedure takes place. It simply moves objects from
// the active heap to the inactive one and fixes the original pointers so they
// start directing to a new place. Collection is started from the root objects
// on the root stack and then on static allocated heap traversing reference tree in depth.
// When collection is done heaps are interchanged so the new one became active.
// All objects that were not moved during the collection are said to be disposed,
// so thier space may be reused by newly allocated ones.
// 
class BakerMemoryManager : public IMemoryAllocator {
private:
    uint32_t  m_gcCount;
    size_t    m_heapSize;
    
    uint8_t*  m_heapOne;
    uint8_t*  m_heapTwo;
    bool      m_activeHeapOne;
    
    uint8_t*  m_inactiveHeapBase;
    uint8_t*  m_inactiveHeapPointer;
    uint8_t*  m_activeHeapBase;
    uint8_t*  m_activeHeapPointer;
    
    TMovableObject* moveObject(TMovableObject* object);
    void collectGarbage();
public:
    BakerMemoryManager(size_t heapSize) : m_gcCount(0), m_activeHeapOne(true) {}
    virtual ~BakerMemoryManager();
    
    virtual void*  allocateMemory(size_t requestedSize);
};

#endif