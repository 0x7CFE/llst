#ifndef LLST_TYPES_H_INCLUDED
#define LLST_TYPES_H_INCLUDED

#include <stdint.h>
#include <sys/types.h>
#include <new>
#include <string>
#include <string.h>

struct TClass;
struct TObject;

//template<struct T> 
inline size_t correctPadding(size_t size) { return (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1); }

// This is a special interpretation of Smalltalk's SmallInteger
// VM handles the special case when object pointer has lowest bit set to 1
// In that case pointer is treated as explicit 31 bit integer equal to (value >> 1)
// Any operation should be done using SmalltalkVM::getIntegerValue() and SmalltalkVM::newInteger()
// Explicit type cast should be strictly avoided for the sake of design stability
typedef uint32_t TInteger;

inline bool     isSmallInteger(TObject* value) { return reinterpret_cast<TInteger>(value) & 1; }
inline uint32_t getIntegerValue(TInteger value) { return (uint32_t) value >> 1; }
inline TInteger newInteger(uint32_t value) { return (value << 1) | 1; }


struct TInstruction {
    uint8_t low;
    uint8_t high;
};

// Helper struct used to hold object size and special status flags
// packed in a 4 bytes space
struct TSize {
private:
    // Raw value holder
    // Do not edit this directly
    uint32_t  data;
    
    static const int FLAG_RELOCATED = 1;
    static const int FLAG_BINARY    = 2;
    static const int FLAGS_MASK     = FLAG_RELOCATED | FLAG_BINARY;
public:
    TSize(uint32_t size, bool isBinary = false, bool isRelocated = false) 
    { 
        data = (size << 2); //) & ~FLAGS_MASK; // masking lowest two bits
        data |= (isBinary << 1); 
        data |= isRelocated; 
    }
    
    TSize(const TSize& size) : data(size.data) { }
    
    uint32_t getSize() const { return data >> 2; }
    uint32_t setSize(uint32_t size) { return data = (data & 3) | (size << 2); }
    bool isBinary() const { return data & FLAG_BINARY; }
    bool isRelocated() const { return data & FLAG_RELOCATED; }
    void setBinary(bool value) { data |= (value << 1); }
    void setRelocated(bool value) { data |= value; }
};


// TObject is the base class for all objects in smalltalk.
// Every object in the system starts with two fields. 
// One holds data size and the other is the pointer to the object's class.
struct TObject {
private:
    // First field of any object is the specially aligned size struct.
    // Two lowest bits determine object binary status (see TByteObject)
    // and relocated status which is used during garbage collection procedure.
    // Depending on the binary status size holds either number of fields
    // or size of objects "tail" which in this case holds raw bytes.
    TSize    size;
    
    // Second field is the pointer to the class which instantinated the object.
    // Every object has a class. Even nil has one. Moreover every class itself
    // is an object too. And yes, it has a class too.
    TClass*  klass;
    
protected:
    // Actual space allocated for object is larger than sizeof(TObject).
    // Remaining space is used to hold user data. For ordinary objects
    // it contains pointers to other objects. For raw binary objects 
    // it is accessed directly as a raw byte array.
    // Actual size is stored in the TSize field and accessed using getSize()
    TObject* fields[0];
    
private:    
    // This class should not be instantinated explicitly
    // Descendants should provide own public InstanceClassName method
    // static const char* InstanceClassName() { return ""; }
public:    
    // this should only be called from Image::readObject
    void setClass(TClass* aClass) { klass = aClass; } 
    
    // By default objects subject to non binary specification
    explicit TObject(uint32_t fieldsCount, TClass* klass, bool isObjectBinary = false) 
        : size(fieldsCount, isObjectBinary), klass(klass) 
    {
        // Zeroing the fields space
        // Binary objects should manage this on their own
        if (!isObjectBinary)
            memset(fields, 0, fieldsCount * sizeof(TObject*));
    }
    
    uint32_t getSize() const { return size.getSize(); }
    TClass*  getClass() const { return klass; }
    
    // delegated methods from TSize
    bool isBinary() const { return size.isBinary(); }
    bool isRelocated() const { return size.isRelocated(); }
    void setRelocated(bool value) { size.setRelocated(value); }
    
    // TODO boundary checks
    TObject** getFields() { return fields; }
    TObject*  getField(uint32_t index) { return fields[index]; }
    TObject*& operator [] (uint32_t index) { return fields[index]; }
    void putField(uint32_t index, TObject* value) { fields[index] = value; }
    
    // Helper function for template instantination
    static bool InstancesAreBinary() { return false; } 
};


// Descendants of this class store raw byte data instead of their instance variables
// The only valid fields are size and class which are inherited from TObject
// Fields space from the TObject is interprited here as raw byte array
struct TByteObject : public TObject {
private:
    // This class should not be instantinated directly
    // Descendants should provide own public InstanceClassName method
    // static const char* InstanceClassName() { return ""; } 
public:
    // Byte objects are said to be binary
    explicit TByteObject(uint32_t dataSize, TClass* klass) : TObject(dataSize, klass, true) 
    {
        // Zeroing data
        memset(fields, 0, dataSize);
    }
    
    uint8_t* getBytes() { return reinterpret_cast<uint8_t*>(fields); }
    uint8_t  getByte(uint32_t index) { return reinterpret_cast<uint8_t*>(fields)[index]; }
    uint8_t& operator [] (uint32_t index) { return reinterpret_cast<uint8_t*>(fields)[index]; }
    
    void putByte(uint32_t index, uint8_t value) { reinterpret_cast<uint8_t*>(fields)[index] = value; }
    
    // Helper function for template instantination
    static bool InstancesAreBinary() { return true; } 
};

struct TByteArray : public TByteObject { 
    static const char* InstanceClassName() { return "ByteArray"; }
};

struct TSymbol : public TByteObject { 
    static const char* InstanceClassName() { return "Symbol"; }
    bool equalsTo(const char* value) { 
        if (!value)
            return false;
        size_t len = strlen(value);
        if (len != getSize()) 
            return false;
        return (memcmp(getBytes(), value, getSize()) == 0);
    }
    std::string toString() { return std::string((const char*)fields, getSize()); }
};

struct TString : public TByteObject { 
    static const char* InstanceClassName() { return "String"; }
};

template <typename Element>
struct TArray : public TObject { 
    TArray(uint32_t capacity, TClass* klass) : TObject(capacity, klass) { }
    static const char* InstanceClassName() { return "Array"; }
    
    template<typename I>
    Element& operator [] (I index) { return (Element&) fields[index]; }
};

struct TMethod;
typedef TArray<TObject*> TObjectArray;
typedef TArray<TSymbol*> TSymbolArray;
typedef TArray<TMethod*> TMethodArray;

struct TContext : public TObject {
    TMethod*      method;
    TObjectArray* arguments;
    TObjectArray* temporaries;
    TObjectArray* stack;
    TInteger      bytePointer;
    TInteger      stackTop;
    TContext*     previousContext;
    
    static const char* InstanceClassName() { return "Context"; }
};

struct TBlock : public TContext {
    TInteger      argumentLocation;
    TContext*     creatingContext;
    TInteger      blockBytePointer;

    static const char* InstanceClassName() { return "Block"; }
};

struct TMethod : public TObject {
    TSymbol*      name;
    TByteObject*  byteCodes;
    TSymbolArray* literals;
    TInteger      stackSize;
    TInteger      temporarySize;
    TClass*       klass;
    TString*      text;
    TObject*      package;
    
    static const char* InstanceClassName() { return "Method"; }
};

struct TDictionary : public TObject {
    TSymbolArray* keys;
    TObjectArray* values;
    static const char*  InstanceClassName() { return "Dictionary"; }
    
    // Find a value associated with a key
    // Returns NULL if nothing was found
    TObject*      find(TSymbol* key);
    TObject*      find(const char* key);
    
private:    
    static int compareSymbols(TSymbol* left, TSymbol* right);
    static int compareSymbols(TSymbol* left, const char* right);
};

struct TClass : public TObject {
    TSymbol*      name;
    TClass*       parentClass;
    TDictionary*  methods;
    TInteger      instanceSize;
    TSymbolArray* variables;
    TObject*      package;
    
    static const char*  InstanceClassName() { return "Class"; }
};

struct TNode : public TObject {
    TObject*      value;
    TNode*        left;
    TNode*        right;
    
    static const char* InstanceClassName() { return "Node"; }
};
    
struct TProcess : public TObject {
    TContext*     context;
    TObject*      state;
    TObject*      result;
    
    static const char* InstanceClassName() { return "Process"; }
};

#endif
