#ifndef LLST_MEMORY_H_INCLUDED
#define LLST_MEMORY_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <vector>

class IMemoryManager {
public:
    virtual bool initializeHeap(size_t heapSize, size_t maxSize = 0) = 0;
    virtual bool initializeStaticHeap(size_t staticHeapSize) = 0;
    
    virtual void* allocate(size_t size) = 0;
    virtual void* staticAllocate(size_t size) = 0;
    virtual void  addStaticRoot(TObject* rootObject) = 0;
    virtual void  collectGarbage() = 0;
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
class BakerMemoryManager : public IMemoryManager {
private:
    uint32_t  m_gcCount;
    size_t    m_heapSize;
    size_t    m_maxHeapSize;
    
    uint8_t*  m_heapOne;
    uint8_t*  m_heapTwo;
    bool      m_activeHeapOne;
    
    uint8_t*  m_inactiveHeapBase;
    uint8_t*  m_inactiveHeapPointer;
    uint8_t*  m_activeHeapBase;
    uint8_t*  m_activeHeapPointer;
    
    size_t    m_staticHeapSize;
    uint8_t*  m_staticHeapBase;
    uint8_t*  m_staticHeapPointer;
    
    // During GC we need to treat all objects in a very simple manner, 
    // just as pointer holders. Class field is also a pointer so we
    // treat it just as one more object field
    struct TMovableObject {
        TSize size;
        TMovableObject* data[0];
    };
    TMovableObject* moveObject(TMovableObject* object);
public:
    BakerMemoryManager() : 
        m_gcCount(0), m_heapSize(0), m_maxHeapSize(0), m_heapOne(0), m_heapTwo(0), 
        m_activeHeapOne(true), m_inactiveHeapBase(0), m_inactiveHeapPointer(0), 
        m_activeHeapBase(0), m_activeHeapPointer(0), m_staticHeapSize(0), 
        m_staticHeapBase(0), m_staticHeapPointer(0)
    { }
    
    virtual ~BakerMemoryManager();
    
    virtual bool  initializeHeap(size_t heapSize, size_t maxHeapSize = 0);
    virtual bool  initializeStaticHeap(size_t staticHeapSize) = 0;
    virtual void* allocate(size_t requestedSize);
    virtual void* staticAllocate(size_t requestedSize);
    virtual void  addStaticRoot(TObject* rootObject);
    virtual void  collectGarbage();
};


class Image {
private:
    int      m_imageFileFD;
    size_t   m_imageFileSize;
    
    void*    m_imageMap;     // pointer to the map base
    uint8_t* m_imagePointer; // sliding pointer
    std::vector<TObject*> m_indirects;
    
    enum TImageRecordType {
        invalidObject = 0,
        ordinaryObject,
        inlineInteger,  // inline 32 bit integer in network byte order
        byteObject,     // 
        previousObject, // link to previously loaded object
        nilObject       // uninitialized (nil) field
    };
    
    uint32_t readWord();
    TObject* readObject();
    bool     openImageFile(const char* fileName);
    void     closeImageFile();
    
    IMemoryManager* m_memoryManager;
public:
    Image(IMemoryManager* manager) 
        : m_imageFileFD(-1), m_imageFileSize(0), 
          m_imagePointer(0), m_memoryManager(manager) 
    {}
    
    bool     loadImage(const char* fileName);
    TObject* getGlobal(const char* name);
    TObject* getGlobal(TSymbol* name);
    
    // GLobal VM objects
};

struct TGlobals {
    TObject* nilObject;
    TObject* trueObject;
    TObject* falseObject;
    TClass*  smallIntClass;
    TClass*  arrayClass;
    TClass*  blockClass;
    TClass*  contextClass;
    TClass*  stringClass;
    TDictionary* globalsObject;
    TMethod* initialMethod;
    TObject* binaryMessages[3];
    TClass*  integerClass;
    TObject* badMethodSymbol;
};

extern TGlobals globals;

#endif