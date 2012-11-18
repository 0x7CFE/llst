#include <sys/types.h>

// typedef uint32_t llstUInt;
// typedef int32_t  llstInt;
// typedef int64_t  llstInt64;

struct TSize {
private:
    uint32_t  data;
    
    const int FLAG_RELOCATED = 1;
    const int FLAG_BINARY    = 2;
    const int FLAGS_MASK     = FLAG_RELOCATED | FLAG_BINARY;
public:
    TSize(uint32_t size, bool isBinary = false, bool isRelocated = false) 
    { 
        data = size & ~FLAGS_MASK; // masking lowest two bits
        data |= (isBinary << 1); 
        data |= isRelocated; 
    }
    
    TSize(const TSize& size) : data(size.data) {}
    
    uint32_t getSize() const { return data & FLAGS_MASK; }
    bool isBinary() const { return data & FLAG_BINARY; }
    bool isRelocated() const { return data & FLAG_RELOCATED; }
    void setBinary(bool value) { data &= (value << 1); }
    void setRelocated(bool value) { data &= value; }
};

struct TClass;
struct TObject {
private:
    TSize    size;
    TClass*  klass;
    const int FIELDS_COUNT = 2;
    
    TObject* data[0];
public:    
    TObject(uint32_t dataCount, const TClass* klass, bool isBinary = false) 
        : size(dataCount + FIELDS_COUNT), klass(klass) 
    { 
        size.setBinary(isBinary);
        
        // TODO Initialize all data as nilObject's
    }
    
    uint32_t getSize() const { return size.getSize(); }
    TClass*  getClass() const { return klass; } 
    
    // delegated methods from TSize
    bool isBinary() const { size.isBinary(); }
    bool isRelocated() const { return size.isRelocated(); }
//     void setBinary(bool value) { size.setBinary(value); }
    void setRelocated(bool value) { size.setRelocated(value); }
    
    // TODO boundary checks
    TObject* getData(uint32_t index) const { return data[index]; }
    TObject* operator [] (uint32_t index) const { return getData(index); }
    void putData(uint32_t index, TObject* value) { data[index] = value; }
    void operator [] (uint32_t index, TObject* value) { return putData(index, value); }
    
    //TODO operator new() {}
};

struct TByteObject : public TObject {
public:
    // TODO boundary checks
    TByteObject(uint32_t dataSize, const TClass* klass) : TObject(dataSize, klass, true) { }
    
    uint8_t* getBytes() { return reinterpret_cast<uint8_t*>(data); }
    
    uint8_t getByte(uint32_t index) const { return reinterpret_cast<uint8_t*>(data)[index]; }
    uint8_t operator [] (uint32_t index) const { return getByte(index); }
    
    void putByte(uint32_t index, uint8_t value) { reinterpret_cast<uint8_t*>(data)[index] = value; }
    uint8_t operator [] (uint32_t index, uint8_t value)  { return putByte(index, value); }
};

// WARNING pointers to this class may actually be values that are SmallInt's
struct TInteger : public TObject { };

struct TContext : public TObject {
    TObject*  method;
    TObject*  arguments;
    TObject*  temporaries;
    TObject*  stack;
    TInteger* bytePointer;
    TInteger* stackTop;
    TContext* previousContext;
    const int FIELDS_COUNT = 7;
    
    TContext() : TObject(FIELDS_COUNT, 0) { /* TODO init fields as nilObject's */ }
};

struct TBlock : public TContext {
    TObject*  argumentLocation;
    TContext* creatingContext;
    TInteger* oldBytePointer;
    const int FIELDS_COUNT = 3;
    
    // TODO method class
    TBlock() : TObject(TContext::FIELDS_COUNT + FIELDS_COUNT, 0) { /* TODO init fields as nilObject's */ }
};

struct TMethod : public TObject {
    TObject*     name;
    TByteObject* byteCodes;
    TObject*     literals;
    TInteger*    stackSize;
    TInteger*    temporarySize;
    TClass*      klass;
    TObject*     text;
    TObject*     package;
    const int    FIELDS_COUNT = 8;
    
    // TODO method class
    TContext() : TObject(FIELDS_COUNT, 0) { /* TODO init fields as nilObject's */ }
};

struct TClass : public TObject {
    TObject*  name;
    TClass*   parentClass;
    TObject*  methods;
    TInteger* instanceSize;
    TObject*  variables;
    TObject*  package;
    const int FIELDS_COUNT = 7;
    
    TClass() : TObject(FIELDS_COUNT, 0) { /* TODO init fields as nilObject's */ }
};

struct TNode : public TObject {
    TObject*  value;
    TObject*  left;
    TObject*  right;
    const int FIELDS_COUNT = 3;
    
    TNode() : TObject(FIELDS_COUNT, 0) { /* TODO init fields as nilObject's */ }
};




