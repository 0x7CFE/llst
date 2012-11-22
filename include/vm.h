#include "types.h"
#include <list>
#include <vector>

class Image {
private:
    size_t imageFileSize;
    
    void*    imageMap;     // pointer to the map base
    uint8_t* imagePointer; // sliding pointer
    int      imageFileFD;
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
    
public:
    Image() : imageFileFD(-1), imageFileSize(0), imagePointer(nullptr) {}
    
    bool loadImage(const char* fileName);
    TObject* getGlobal(const char* name);
    
    // GLobal VM objects
    static struct {
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
    } globals;
};

class SmalltalkVM {
public:
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
    
    enum TExecuteResult {
        returnError = 2,
        returnBadMethod,
        returnReturned,
        returnTimeExpired,
        returnBreak
    }; 
    
    std::list<TObject*> m_rootStack;
    Image m_image;
    
    struct TMethodCacheEntry
    {
        TObject* methodName;
        TClass*  receiverClass;
        TMethod* method;
    };
    
    const int LOOKUP_CACHE_SIZE = 4096;
    TMethodCacheEntry m_lookupCache[LOOKUP_CACHE_SIZE];
    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;

    uint32_t getIntegerValue(TInteger value) { return (uint32_t) value >> 1; }
    TInteger newInteger(uint32_t value) { return (value << 1) | 1; }

    // lexicographic comparison of two byte objects
//     int compareSymbols(const TByteObject* left, const TByteObject* right);
    
    // locate the method in the hierarchy of the class
    TMethod* lookupMethod(const TObject* selector, const TObject* klass);
    
    // fast method lookup in the method cache
    TMethod* lookupMethodInCache(const TObject* selector, const TObject* klass);
    
    // flush the method lookup cache
    void flushCache();
    
    int execute(TProcess* process, uint32_t ticks);
    void doPushConstant(uint8_t constant, TArray* stack, uint32_t& stackTop);
    void doSendMessage(TContext* context, TMethod* method);
    
    template<class T> T* newObject(size_t objectSize = 0);
public:
    
};

