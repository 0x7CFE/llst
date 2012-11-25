#ifndef LLST_VM_H_INCLUDED
#define LLST_VM_H_INCLUDED

#include <list>
#include <vector>

#include <types.h>
#include <memory.h>
#include <stdlib.h>

class Image {
private:
    int    imageFileFD;
    size_t imageFileSize;
    
    void*    imageMap;     // pointer to the map base
    uint8_t* imagePointer; // sliding pointer
    std::vector<TObject*> indirects; // TODO preallocate space
    
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
    
    IMemoryAllocator* m_memoryAllocator;
public:
    Image(IMemoryAllocator* allocator) 
        : imageFileFD(-1), imageFileSize(0), 
          imagePointer(0), m_memoryAllocator(allocator) 
    {}
    
    bool loadImage(const char* fileName);
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
    TObject* binaryMessages[3]; // NOTE
    TClass*  integerClass;
    TObject* badMethodSymbol;
};

extern TGlobals globals;

class SmalltalkVM {
public:
    enum TExecuteResult {
        returnError = 2,
        returnBadMethod,
        returnReturned,
        returnTimeExpired,
        returnBreak,
        
        returnNoReturn = 255
    }; 
    
    TExecuteResult execute(TProcess* process, uint32_t ticks);
    SmalltalkVM() : m_image(0) {}
    
private:
    enum {
        extended = 0,
        pushInstance,
        pushArgument,    
        pushTemporary,   
        pushLiteral,     
        pushConstant,    
        assignInstance,  
        assignTemporary, 
        markArguments,   
        sendMessage,     
        sendUnary,       
        sendBinary,      
        pushBlock,       
        doPrimitive,     
        doSpecial       
    };
    
    enum Special {
        SelfReturn = 1,
        StackReturn,
        BlockReturn,
        Duplicate,
        PopTop,
        Branch,
        BranchIfTrue,
        BranchIfFalse,
        SendToSuper = 11,
        Breakpoint = 12
    };
    
    enum {
        nilConst = 10,
        trueConst,
        falseConst
    };
    
    enum TClassID {
        Object,
        Class,
        Method,
        Context,
        Process,
        Array,
        Dictionary,
        Block,
    };
    
    TClass* getRootClass(TClassID id);
    
    std::list<TObject*> m_rootStack;
    //HeapMemoryManager staticMemoryManager(); TODO
    Image m_image; //TODO
    
    struct TMethodCacheEntry
    {
        TObject* methodName;
        TClass*  receiverClass;
        TMethod* method;
    };
    
    static const unsigned int LOOKUP_CACHE_SIZE = 4096;
    TMethodCacheEntry m_lookupCache[LOOKUP_CACHE_SIZE];
    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;

    // lexicographic comparison of two byte objects
//     int compareSymbols(const TByteObject* left, const TByteObject* right);
    
    // locate the method in the hierarchy of the class
    TMethod* lookupMethod(TSymbol* selector, TClass* klass);
    
    // fast method lookup in the method cache
    TMethod* lookupMethodInCache(TSymbol* selector, TClass* klass);
    
    // flush the method lookup cache
    void flushCache();
    
    void doPushConstant(uint8_t constant, TObjectArray& stack, uint32_t& stackTop);
    
    void doSendMessage(
        TSymbol* selector, 
        TObjectArray& arguments, 
        TContext* context, 
        uint32_t& stackTop);
    
    TObject* doExecutePrimitive(
        uint8_t opcode, 
        TObjectArray& stack, 
        uint32_t& stackTop,
        TProcess& process);
    
    TExecuteResult doDoSpecial(
        TInstruction instruction, 
        TContext* context, 
        uint32_t& stackTop,
        TMethod*& method,
        uint32_t& bytePointer,
        TProcess*& process,
        TObject*& returnedValue);
    
    //template<class T> TClass* getClass(TObject* object);
public:
    
    template<class T> T* newObject(size_t objectSize = 0);
    TObject* newObject(TSymbol* className, size_t objectSize);
    TObject* newObject(TClass* klass);
    
};

template<class T> T* SmalltalkVM::newObject(size_t objectSize /*= 0*/)
{
    // TODO fast access to common classes
    TClass* klass = (TClass*) m_image.getGlobal(T::InstanceClassName());
    if (!klass)
        return (T*) globals.nilObject;
    
    // Slot size is computed depending on the object type
    size_t slotSize = 0;
    if (T::InstancesAreBinary())    
        slotSize = sizeof(T) + objectSize;
    else 
        slotSize = sizeof(T) + objectSize * sizeof(T*);
        
    void* objectSlot = malloc(slotSize); // TODO llvm_gc_allocate
    if (!objectSlot)
        return (T*) globals.nilObject;
    
    T* instance = (T*) new (objectSlot) TObject(objectSize, klass);
    if (! T::InstancesAreBinary())     
    {
        for (uint32_t i = 0; i < objectSize; i++)
            instance->putField(i, globals.nilObject);
    }
    
    return instance;
}

// Specialization of newObject for array
template<> TObjectArray* SmalltalkVM::newObject<TObjectArray>(size_t objectSize /*= 0*/);


#endif
