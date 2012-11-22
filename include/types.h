#ifndef LLST_TYPES_H_INCLUDED
#define LLST_TYPES_H_INCLUDED

#include <stdint.h>
#include <sys/types.h>
#include <new>
#include <string.h>

// This is a special interpretation of Smalltalk's SmallInteger
// VM handles the special case when object pointer has lowest bit set to 1
// In that case pointer is treated as explicit 31 bit integer equal to (value >> 1)
// Any operation should be done using SmalltalkVM::getIntegerValue() and SmalltalkVM::newInteger()
// Explicit type cast should be strictly avoided for the sake of design stability
typedef uint32_t TInteger;

using namespace std;

struct TInstruction {
    uint8_t low;
    uint8_t high;
};

struct TClass;
struct TObject {
private:
    struct TSize {
    private:
        uint32_t  data;
        
        static const int FLAG_RELOCATED = 1;
        static const int FLAG_BINARY    = 2;
        static const int FLAGS_MASK     = FLAG_RELOCATED | FLAG_BINARY;
    public:
        TSize(uint32_t size, bool isBinary = false, bool isRelocated = false) 
        { 
            data = size & ~FLAGS_MASK; // masking lowest two bits
            data |= (isBinary << 1); 
            data |= isRelocated; 
        }
        
        TSize(const TSize& size) : data(size.data) { }
        
        uint32_t getSize() const { return data >> 2; }
        bool isBinary() const { return data & FLAG_BINARY; }
        bool isRelocated() const { return data & FLAG_RELOCATED; }
        void setBinary(bool value) { data &= (value << 1); }
        void setRelocated(bool value) { data &= value; }
    };
    
    TSize    size;
    TClass*  klass;
    
    
    // This class should not be instantinated explicitly
    // Descendants should provide own public className method
    static const char* className() { return ""; }
protected:
    TObject* data[0];
    
//     friend class Image;
//     friend TObject* Image::readObject();
public:    
    // this should only be called from Image::readObject
    void setClass(TClass* aClass) { klass = aClass; } 
    
    // By default objects subject to non binary specification
    static bool isBinary() { return false; } 
    
    TObject(uint32_t dataCount, TClass* klass, bool isBinary = false) 
        : size(dataCount), klass(klass) 
    { 
        size.setBinary(isBinary);
        
        // TODO Initialize all data as nilObject's
    }
    
    uint32_t getSize() const { return size.getSize(); }
    TClass*  getClass() const { return klass; } 
    
    // delegated methods from TSize
//     bool isBinary() const { return size.isBinary(); }
    bool isRelocated() const { return size.isRelocated(); }
    void setRelocated(bool value) { size.setRelocated(value); }
    
    // TODO boundary checks
    TObject* getField(uint32_t index) { return data[index]; }
    TObject*& operator [] (uint32_t index) { return data[index]; }
    void putField(uint32_t index, TObject* value) { data[index] = value; }
//     void operator [] (uint32_t index, TObject* value) { return putField(index, value); }
};


// Descendants of this class store raw byte data instead of their instance variables
// The only valid fields are size and class which are inherited from TObject
struct TByteObject : public TObject {
private:
    // This class should not be instantinated directly
    // Descendants should provide own public className method
    static const char* className() { return ""; } 
public:
    // Byte objects are said to be binary
    static bool isBinary() { return true; } 
    
    TByteObject(uint32_t dataSize, TClass* klass) : TObject(dataSize, klass, true) { }
    
    uint8_t* getBytes() { return reinterpret_cast<uint8_t*>(data); }
    
    uint8_t getByte(uint32_t index) { return reinterpret_cast<uint8_t*>(data)[index]; }
    uint8_t& operator [] (uint32_t index) { return reinterpret_cast<uint8_t*>(data)[index]; }
    
    void putByte(uint32_t index, uint8_t value) { reinterpret_cast<uint8_t*>(data)[index] = value; }
    //uint8_t operator [] (uint32_t index, uint8_t value)  { return putByte(index, value); }
};

struct TByteArray : public TByteObject { 
    static const char* className() { return "ByteArray"; }
};

struct TSymbol : public TByteObject { 
    static const char* className() { return "Symbol"; }
    bool equalsTo(const char* value) { 
        if (!value)
            return false;
        size_t len = strlen(value);
        if (len != getSize()) 
            return false;
        return (memcmp(getBytes(), value, getSize()) == 0);
    }
};

struct TString : public TByteObject { 
    static const char* className() { return "String"; }
};

struct TArray : public TObject { 
    TArray(uint32_t capacity, TClass* klass) : TObject(capacity, klass) { }
    static const char* className() { return "Array"; }
};

struct TMethod;
struct TContext : public TObject {
    TMethod*     method;
    TArray*      arguments;
    TArray*      temporaries;
    TArray*      stack;
    TInteger     bytePointer;
    TInteger     stackTop;
    TContext*    previousContext;
    
    static const char* className() { return "Context"; }
};

struct TBlock : public TContext {
    TObject*     argumentLocation;
    TContext*    creatingContext;
    TInteger     oldBytePointer;

    static const char* className() { return "Block"; }
};

struct TMethod : public TObject {
    TObject*     name;
    TByteObject* byteCodes;
    TArray*      literals;
    TInteger     stackSize;
    TInteger     temporarySize;
    TClass*      klass;
    TObject*     text;
    TObject*     package;
    
    static const char* className() { return "Method"; }
};

struct TDictionary : public TObject {
    TArray*      keys;
    TArray*      values;
    static const char* className() { return "Dictionary"; }
    
    // Find a value associated with a key
    // Returns NULL if nothing was found
    TObject*     find(TSymbol* key);
    TObject*     find(const char* key);
private:    
    static int compareSymbols(TSymbol* left, TSymbol* right);
    static int compareSymbols(TSymbol* left, const char* right);
    
};

struct TClass : public TObject {
    TObject*     name;
    TClass*      parentClass;
    TDictionary* methods;
    TInteger     instanceSize;
    TArray*      variables;
    TObject*     package;
    
    static const char* className() { return "Class"; }
};

struct TNode : public TObject {
    TObject*     value;
    TNode*       left;
    TNode*       right;
    
    static const char* className() { return "Node"; }
};
    
struct TProcess : public TObject {
    TContext*    context;
    TObject*     state;
    TObject*     result;
    
    static const char* className() { return "Process"; }
};

#endif
