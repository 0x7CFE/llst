#ifndef LLST_TYPES_H_INCLUDED
#define LLST_TYPES_H_INCLUDED

#include <stdint.h>
#include <sys/types.h>
#include <new>
#include <string>
#include <string.h>

struct TClass;
struct TObject;

// All our objects needed to be aligned by the 4 bytes at least.
// This function helps to calculate the buffer size enough to fit the data,
// yet being a multiple of 4 (or 8 depending on the pointer size)
inline size_t correctPadding(size_t size) { return (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1); }
//inline size_t correctPadding(size_t size) { return (size + 3) & ~3; }

// This is a special interpretation of Smalltalk's SmallInteger
// VM handles the special case when object pointer has lowest bit set to 1
// In that case pointer is treated as explicit 31 bit integer equal to (value >> 1)
// Any operation should be done using SmalltalkVM::getIntegerValue() and SmalltalkVM::newInteger()
// Explicit type cast should be strictly avoided for the sake of design and stability
// TODO May be we should refactor TInteger to a class which provides cast operators.
typedef int32_t TInteger;

// Helper functions for TInteger operation
inline bool     isSmallInteger(TObject* value) { return reinterpret_cast<TInteger>(value) & 1; }
inline int32_t  getIntegerValue(TInteger value) { return (int32_t) (value >> 1); }
inline TInteger newInteger(int32_t value) { return (value << 1) | 1; }

// TInstruction represents one decoded Smalltalk instruction.
// Actual meaning of parts is determined during the execution.
struct TInstruction {
    uint8_t low;
    uint8_t high;
};

// Helper struct used to hold object size and special 
// status flags packed in a 4 bytes space. TSize is used
// in the TObject hierarchy and in the TMovableObject in GC
struct TSize {
private:
    // Raw value holder. Do not edit this value directly
    uint32_t data;
    
    static const int FLAG_RELOCATED = 1;
    static const int FLAG_BINARY    = 2;
    static const int FLAGS_MASK     = FLAG_RELOCATED | FLAG_BINARY;
public:
    TSize(uint32_t size, bool isBinary = false, bool isRelocated = false) 
    { 
        data  = (size << 2);
        data |= isBinary    ? FLAG_BINARY : 0; 
        data |= isRelocated ? FLAG_RELOCATED : 0; 
    }
    
    TSize(const TSize& size) : data(size.data) { }
    
    uint32_t getSize() const { return data >> 2; }
    uint32_t setSize(uint32_t size) { return data = (data & 3) | (size << 2); }
    bool isBinary() const { return data & FLAG_BINARY; }
    bool isRelocated() const { return data & FLAG_RELOCATED; }
    void setBinary() { data |= FLAG_BINARY; }
    void setRelocated() { data |= FLAG_RELOCATED; }
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
    static const char* InstanceClassName() { return ""; }
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
    static const char* InstanceClassName() { return ""; } 
public:
    // Byte objects are said to be binary
    explicit TByteObject(uint32_t dataSize, TClass* klass) : TObject(dataSize, klass, true) 
    {
        // Zeroing byte object data
        memset((void*)fields, 0, dataSize);
    }
    
    uint8_t* getBytes() { return reinterpret_cast<uint8_t*>(fields); }
    uint8_t  getByte(uint32_t index) { return reinterpret_cast<uint8_t*>(fields)[index]; }
    uint8_t& operator [] (uint32_t index) { return reinterpret_cast<uint8_t*>(fields)[index]; }
    
    void putByte(uint32_t index, uint8_t value) { reinterpret_cast<uint8_t*>(fields)[index] = value; }
    
    // Helper function for template instantination
    static bool InstancesAreBinary() { return true; } 
};


// ByteArray represents Smalltalk's ByteArray class
// It does not provide any new methods 
struct TByteArray : public TByteObject { 
    static const char* InstanceClassName() { return "ByteArray"; }
};

// TSymbol represents Smalltalk's Symbol class. In most cases symbols
// may be treated as usual strings except that every instance of Symbol
// is unique. I.e. there are no two equal symbols in the image. All references
// to the similar symbols are practically point to the single object.
// #helloWorld will always be == to ('hello'+'World') asSymbol,
// whereas 'hello' + 'World' will not be == 'helloWorld' because strings are 
// different objects. This is achieved by providing custom implementation of
// method new: in the MetaSymbol class:
// 
// METHOD MetaSymbol
// new: fromString | sym |
//      ^ symbols at: fromString
//      ifAbsent: [ symbols add: (self intern: fromString) ]
// 
// Be careful not to use asSymbol exceedingly especial in loops or other places where
// many different instances of Symbol may be created.
// 
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

// TString represents the Smalltalk's String class. 
// Strings are binary objects that hold raw character bytes. 
struct TString : public TByteObject { 
    static const char* InstanceClassName() { return "String"; }
};

// Chars are intermediate representation of single printable character
// When #String>>at: method is called an instance of Char is returned.
// Note that actually String is NOT the array of Chars. String is binary
// object holding raw character bytes which then interpreted as characters
struct TChar : public TObject {
    TInteger value;
    static const char* InstanceClassName() { return "Char"; }
};

// TArray represents the Smalltalk's Array class.
// Arrays are ordinary objects except that their field space
// is used to store pointers to arbitary objects. Access to
// the data is performed by the integer index.
// 
// llst defines TArray class as a template. Please use provided standard
// typedefs instead of bare TArray<TObject*>. This will help to eliminate
// various errors in VM code where object of specific type is expected but
// incorrect array is used to get it.
// 
// NOTE: Unlike C languages, indexing in Smalltalk is started from the 1. 
//       So the first element will have index 1, the second 2 and so on.
template <typename Element>
struct TArray : public TObject { 
    TArray(uint32_t capacity, TClass* klass) : TObject(capacity, klass) { }
    static const char* InstanceClassName() { return "Array"; }
    
    Element getField(uint32_t index) { return (Element) fields[index]; }
    
    template<typename I>
    Element& operator [] (I index) { return (Element&) fields[index]; }
};

struct TMethod;
typedef TArray<TObject*> TObjectArray;
typedef TArray<TSymbol*> TSymbolArray;
typedef TArray<TMethod*> TMethodArray;


// Context class is the heart of Smalltalk's VM execution mechanism.
// Basicly, it holds all information needed to execute a method.
// It contains the arguments passed to the method, stack space, array which
// will hold temporary objects during the call dispatching and the pointers
// to the current executing instruction and the stack top.
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

// In Smalltalk, a block is a piece of code that may be executed by sending
// a #value or #value: message to it. From the VM's point of view blocks are
// nested contexts that are linked to the wrapping method. Block has direct access to 
// the lexical context of the wrapping method and it's variables. This is needed to
// implement the closure mechanism.

// TBlock is a direct descendant of TContext class which adds fields specific to blocks
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

// Dictionary is a simple associative container which holds pairs
// of keys and associated values. Keys are represented by symbols,
// whereas values may be instances of any class.
// 
// Technically, Dictionary is implemented as two parallel arrays:
// keys[] and values[]. keys[] stores sorted symbols. values[] holds 
// objects corresponding to the key in the same position.
// 
// Because keys are sorted, we may perform a search as a binary search.
struct TDictionary : public TObject {
    TSymbolArray* keys;
    TObjectArray* values;
    
    // Find a value associated with a key
    // Returns NULL if nothing was found
    TObject*      find(TSymbol* key);
    TObject*      find(const char* key);
    
    static const char* InstanceClassName() { return "Dictionary"; }
private:
    // Helper comparison functions. Compares the two symbols 'left' and 'right' 
    // (or it's string representation). Returns an integer less than, equal to,
    // or greater than zero if 'left' is found, respectively, to be less than,
    // to match, or be greater than 'right'.
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
