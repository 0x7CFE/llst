#ifndef LLST_HELPER_VM_IMAGE_INCLUDED
#define LLST_HELPER_VM_IMAGE_INCLUDED

#include <types.h>
#include <memory.h>

class H_VMImage
{
    std::auto_ptr<IMemoryManager> m_memoryManager;
    std::auto_ptr<Image> m_smalltalkImage;
public:
    H_VMImage(const std::string& imageName) {
        std::auto_ptr<IMemoryManager> memoryManager(new BakerMemoryManager());
        memoryManager->initializeHeap(1024*1024, 1024*1024);
        std::auto_ptr<Image> smalltalkImage(new Image(memoryManager.get()));
        smalltalkImage->loadImage(TESTS_DIR "./data/" + imageName + ".image");
    }
    TObjectArray* newArray(std::size_t fields);
    template<class T> void deleteObject(T* object);
private:
    TObject* newOrdinaryObject(TClass* klass, std::size_t slotSize);
};

inline TObject* H_VMImage::newOrdinaryObject(TClass* klass, std::size_t slotSize)
{
    void* objectSlot = new char[ correctPadding(slotSize) ];

    // Object size stored in the TSize field of any ordinary object contains
    // number of pointers except for the first two fields
    std::size_t fieldsCount = slotSize / sizeof(TObject*) - 2;

    TObject* instance = new (objectSlot) TObject(fieldsCount, klass);

    for (uint32_t index = 0; index < fieldsCount; index++)
        instance->putField(index, globals.nilObject);

    return instance;
}

inline TObjectArray* H_VMImage::newArray(std::size_t fields)
{
    return static_cast<TObjectArray*>( newOrdinaryObject(globals.arrayClass, sizeof(TObjectArray) + fields * sizeof(TObject*)) );
}

template<class T>
inline void H_VMImage::deleteObject(T* object) {
    char* mem = reinterpret_cast<char*>(object);
    delete[] mem;
}

#endif
