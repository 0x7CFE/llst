#ifndef LLST_MEMORY_H_INCLUDED
#define LLST_MEMORY_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <vector>
#include <list>

class IMemoryManager {
public:
    virtual bool initializeHeap(size_t heapSize, size_t maxSize = 0) = 0;
    virtual bool initializeStaticHeap(size_t staticHeapSize) = 0;
    
    virtual void* allocate(size_t size, bool* collectionOccured = 0) = 0;
    virtual void* staticAllocate(size_t size) = 0;
    virtual void  addStaticRoot(TObject* rootObject) = 0;
    virtual void  collectGarbage() = 0;
    
    // External pointer handling
    virtual void  registerExternalPointer(TObject** pointer) = 0;
    virtual void  releaseExternalPointer(TObject** pointer) = 0;
    
    virtual uint32_t allocsBeyondCollection() = 0;
};

template <typename O> class hptr {
public:
    typedef O Object;
private:
    Object* target;
    IMemoryManager* mm;
public:
    hptr(Object* object, IMemoryManager* mm, bool notRegister = false) : target(object), mm(mm) 
    {
        if (mm) mm->registerExternalPointer((TObject**) &object);
    }
    
    hptr(const hptr<O>& pointer) : target(pointer.target), mm(pointer.mm) 
    {
        if (mm) mm->registerExternalPointer((TObject**) &target);
    }
    
    ~hptr() { if (mm) mm->releaseExternalPointer((TObject**) &target); }
    
    hptr<Object>& operator = (hptr<Object>& pointer) { target = pointer.target; return *this; }
    Object* operator = (Object* object) { target = object; return object; }
    
    Object* rawptr() const { return target; }
    Object* operator -> () const { return target; }
    Object& (operator*)() const { return *target; }
    operator Object*() const { return target; }
    
    template<typename T> 
    T* cast() { return (T*) target; }

//      template<typename I>
//      typeof(target->operator[](I()))& operator [] (I index) const { return target->operator[](index); }
     
//     template<typename I>
//     typeof(target->operator[](1))& operator [] (I index) const { return target->operator[](index); }
};

template<typename T> class hptr< TArray<T> >
{
public:
    typedef TArray<T> Object;
private:
    Object* target;
    IMemoryManager* mm;
public:
    hptr(Object* object, IMemoryManager* mm, bool notRegister = false) : target(object), mm(mm) 
    {
        if (mm) mm->registerExternalPointer((TObject**) &object);
    }
    
    hptr(const hptr<Object>& pointer) : target(pointer.target), mm(pointer.mm) 
    {
        if (mm) mm->registerExternalPointer((TObject**) &target);
    }
    
    ~hptr() { if (mm) mm->releaseExternalPointer((TObject**) &target); }
    
   // hptr* operator = (const hptr<Object>& pointer) { target = pointer.target; return this; }
   // hptr* operator = (const Object* object) { target = object; return this; }
    
    Object* rawptr() const { return target; }
    Object* operator -> () const { return target; }
    Object& (operator*)() const { return *target; }
    operator Object*() const { return target; }
    
    template<typename I>
    T& operator [] (I index) const { return target->operator[](index); }
};

template<> class hptr<TByteObject>
{
public:
    typedef TByteObject Object;
private:
    Object* target;
    IMemoryManager* mm;
public:
    hptr(Object* object, IMemoryManager* mm, bool notRegister = false) : target(object), mm(mm) 
    {
        if (mm) mm->registerExternalPointer((TObject**) &object);
    }
    
    hptr(const hptr& pointer) : target(pointer.target), mm(pointer.mm) 
    {
        if (mm) mm->registerExternalPointer((TObject**) &target);
    }
    
    ~hptr() { if (mm) mm->releaseExternalPointer((TObject**) &target); }
    
    hptr* operator = (const hptr<Object>& pointer) { target = pointer.target; return this; }
    //hptr* operator = (const Object* object) { target = object; return this; }
    
    Object* rawptr() const { return target; }
    Object* operator -> () const { return target; }
    Object& (operator*)() const { return *target; }
    operator Object*() const { return target; }
    
    //template<typename I>
    uint8_t& operator [] (uint32_t index) const { return target->operator[](index); }
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
    uint32_t  m_allocsBeyondGC;
    
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

    struct TRootPointers {
        uint32_t size;
        uint32_t top;
        TObject* data[0];
    };
    
    // During GC we need to treat all objects in a very simple manner, 
    // just as pointer holders. Class field is also a pointer so we
    // treat it just as one more object field
    struct TMovableObject {
        TSize size;
        TMovableObject* data[0];
    };
    TMovableObject* moveObject(TMovableObject* object);
    
    // This contains an array of pointers of objects from the
    // static heap to the dynamic one. It is used during the GC
    // as a root for pointer iteration.
    //TObjectArray* m_staticRoots;
    
    // FIXME temporary solution before GC will prove it's working
    std::vector<TMovableObject*> m_staticRoots;
    //std::hash_map<TObject*, TObject*> m_staticRoots;
    
    typedef std::list<TMovableObject**> TPointerList;
    typedef std::list<TMovableObject**>::iterator TPointerIterator;
    TPointerList m_externalPointers;
    
public:
    BakerMemoryManager();
    virtual ~BakerMemoryManager();
    
    virtual bool  initializeHeap(size_t heapSize, size_t maxHeapSize = 0);
    virtual bool  initializeStaticHeap(size_t staticHeapSize);
    virtual void* allocate(size_t requestedSize, bool* gcOccured = 0);
    virtual void* staticAllocate(size_t requestedSize);
    virtual void  addStaticRoot(TObject* rootObject);
    virtual void  collectGarbage();
    
    // External pointer handling
    virtual void  registerExternalPointer(TObject** pointer);
    virtual void  releaseExternalPointer(TObject** pointer);
    
    // Returns amount of allocations that were done after last GC
    // May be used as a flag that GC had just took place
    virtual uint32_t allocsBeyondCollection() { return m_allocsBeyondGC; }
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