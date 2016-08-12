#ifndef LLST_HELPER_VM_IMAGE_INCLUDED
#define LLST_HELPER_VM_IMAGE_INCLUDED

#include <types.h>
#include <memory.h>

#include <cstring>

class H_VMImage
{
    std::auto_ptr<IMemoryManager> m_memoryManager;
    std::auto_ptr<Image> m_smalltalkImage;
public:
    H_VMImage(const std::string& imageName) :
        m_memoryManager(new BakerMemoryManager()),
        m_smalltalkImage(new Image(m_memoryManager.get()))
    {
        m_memoryManager->initializeHeap(1024*1024, 1024*1024);
        m_smalltalkImage->loadImage(TESTS_DIR "./data/" + imageName + ".image");
    }
    TObjectArray* newArray(std::size_t fields);
    TString* newString(std::size_t size);
    TString* newString(const std::string& str);
    template<class T> void deleteObject(T* object);
private:
    TObject* newOrdinaryObject(TClass* klass, std::size_t slotSize);
    TByteObject* newBinaryObject(TClass* klass, std::size_t dataSize);
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

inline TByteObject* H_VMImage::newBinaryObject(TClass* klass, std::size_t dataSize)
{
    // All binary objects are descendants of ByteObject
    // They could not have ordinary fields, so we may use it
    uint32_t slotSize = sizeof(TByteObject) + dataSize;

    void* objectSlot = new char[ correctPadding(slotSize) ];
    TByteObject* instance = new (objectSlot) TByteObject(dataSize, klass);

    return instance;
}

inline TObjectArray* H_VMImage::newArray(std::size_t fields)
{
    return static_cast<TObjectArray*>( newOrdinaryObject(globals.arrayClass, sizeof(TObjectArray) + fields * sizeof(TObject*)) );
}

inline TString* H_VMImage::newString(std::size_t size)
{
    return static_cast<TString*>( newBinaryObject(globals.stringClass, size) );
}

inline TString* H_VMImage::newString(const std::string& str)
{
    TString* result = newString(str.size());
    std::memcpy(result->getBytes(), str.c_str(), str.size());
    return result;
}

template<class T>
inline void H_VMImage::deleteObject(T* object) {
    char* mem = reinterpret_cast<char*>(object);
    delete[] mem;
}

#endif
