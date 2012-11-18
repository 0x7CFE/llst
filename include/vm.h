#include <sys/types.h>

// typedef uint32_t llstUInt;
// typedef int32_t  llstInt;
// typedef int64_t  llstInt64;

struct TSize {
private:
    uint32_t data;
    
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

struct TObject {
private:
    TSize    size;
    TObject* klass;
    TObject* data[0];
public:    
    TObject(uint32_t size, const TObject* klass, bool isBinary = false) 
        : size(size), klass(klass) 
    { 
        size.setBinary(isBinary); 
    }
    
    uint32_t getSize() const { return size.getSize(); }
    TObject* getClass() const { return klass; } 
    
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
};

struct TByteObject : public TObject {
public:
    // TODO boundary checks
    TByteObject(uint32_t size, const TObject* klass) : TObject(size, klass, true) { }
    
    uint8_t getByte(uint32_t index) { return reinterpret_cast<uint8_t*>(data)[index]; }
    uint8_t operator [] (uint32_t index) const { return getByte(index); }
    
    void putByte(uint32_t index, uint8_t value) { reinterpret_cast<uint8_t*>(data)[index] = value; }
    uint8_t operator [] (uint32_t index) { return getByte(index); }
};


