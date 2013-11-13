/*
 *    types.h
 *
 *    Basic Smalltalk related types and structures
 *
 *    LLST (LLVM Smalltalk or Lo Level Smalltalk) version 0.1
 *
 *    LLST is
 *        Copyright (C) 2012 by Dmitry Kashitsyn   aka Korvin aka Halt <korvin@deeptown.org>
 *        Copyright (C) 2012 by Roman Proskuryakov aka Humbug          <humbug@deeptown.org>
 *
 *    LLST is based on the LittleSmalltalk which is
 *        Copyright (C) 1987-2005 by Timothy A. Budd
 *        Copyright (C) 2007 by Charles R. Childers
 *        Copyright (C) 2005-2007 by Danny Reinhold
 *
 *    Original license of LittleSmalltalk may be found in the LICENSE file.
 *
 *
 *    This file is part of LLST.
 *    LLST is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    LLST is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with LLST.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LLST_TYPES_H_INCLUDED
#define LLST_TYPES_H_INCLUDED

#include <stdint.h>
#include <sys/types.h>
#include <new>
#include <string>
#include <sstream>

struct TClass;
struct TObject;
struct TMethod;

// All our objects needed to be aligned by the 4 bytes at least.
// This function helps to calculate the buffer size enough to fit the data,
// yet being a multiple of 4 (or 8 depending on the pointer size)
inline std::size_t correctPadding(std::size_t size) { return (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1); }
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
inline int32_t  getIntegerValue(TInteger value) { return (value >> 1); }
inline TInteger newInteger(int32_t value) { return (value << 1) | 1; }

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
    TSize(uint32_t size, bool binary = false, bool relocated = false)
    {
        data  = (size << 2);
        data |= binary    ? FLAG_BINARY : 0;
        data |= relocated ? FLAG_RELOCATED : 0;
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
    union {
        TObject* fields[0];
        uint8_t  bytes[0];
    };

private:
    // This class should not be instantinated explicitly
    // Descendants should provide own public InstanceClassName method
    static const char* InstanceClassName() { return ""; }
public:
    // this should only be called from Image::readObject
    void setClass(TClass* aClass) { klass = aClass; }

    // By default objects subject to non binary specification
    explicit TObject(uint32_t fieldsCount, TClass* klass, bool isObjectBinary = false)
        : size(fieldsCount, isObjectBinary), klass(klass) { }

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

    // Helper constant for template instantination
    enum { InstancesAreBinary = false };
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
    explicit TByteObject(uint32_t dataSize, TClass* klass) : TObject(dataSize, klass, true) { }

    uint8_t* getBytes() { return bytes; }
    const uint8_t* getBytes() const { return bytes; }
    const uint8_t getByte(uint32_t index) const { return bytes[index]; }
    uint8_t& operator [] (uint32_t index) { return bytes[index]; }

    void putByte(uint32_t index, uint8_t value) { bytes[index] = value; }

    // Helper constant for template instantination
    enum { InstancesAreBinary = true };
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
    std::string toString() const { return std::string(reinterpret_cast<const char*>(bytes), getSize()); }

    // Helper comparison function functional object. Compares two symbols (or it's string representation).
    // Returns true when 'left' is found to be less than 'right'.
    struct TCompareFunctor {
        // This function compares two byte objects depending on their lenght and contents
        bool operator() (const TSymbol* left, const TSymbol* right) const;
        // This function compares byte object and null terminated string depending on their lenght and contents
        bool operator() (const TSymbol* left, const char* right) const;
        bool operator() (const char* left, const TSymbol* right) const;
    };
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
template <typename Element>
struct TArray : public TObject {
    TArray(uint32_t capacity, TClass* klass) : TObject(capacity, klass) { }
    static const char* InstanceClassName() { return "Array"; }

    Element* getField(uint32_t index) { return static_cast<Element*>(fields[index]); }
    template <typename Type> Type* getField(uint32_t index) { return static_cast<Type*>(fields[index]); }

    // NOTE: Unlike C languages, indexing in Smalltalk is started from the 1.
    //       So the first element will have index 1, the second 2 and so on.
    template<typename I>
    Element*& operator [] (I index) {
        // compile-time check whether Element is in the type tree of TObject
        (void) static_cast<Element*>( reinterpret_cast<TObject*>(0) );

        TObject** field   = &fields[index];
        Element** element = reinterpret_cast<Element**>(field);
        return *element;
    }
};

typedef TArray<TObject> TObjectArray;
typedef TArray<TSymbol> TSymbolArray;

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
// the lexical context of the wrapping method and its variables. This is needed to
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
// of keys and associated values. Keys are represented with symbols,
// whereas values may be instances of any class.
//
// Technically, Dictionary is implemented as two parallel arrays:
// keys[] and values[]. keys[] store sorted symbols. values[] hold
// objects corresponding to the key at the same position.
//
// Because keys are sorted, we may perform a search as a binary search.
struct TDictionary : public TObject {
    TSymbolArray* keys;
    TObjectArray* values;

    // Find a value associated with a key
    // Returns NULL if nothing was found
    // Explicit instantination of:
    // find(const TSymbol* key)
    // find(const char* key)
    template<typename K> TObject* find(const K* key) const;
    template<typename T, typename K> T* find(const K* key) const { return static_cast<T*>(find(key)); }

    static const char* InstanceClassName() { return "Dictionary"; }
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

// TInstruction represents one decoded Smalltalk instruction.
// Actual meaning of parts is determined during the execution.
struct TInstruction {
    uint8_t low;
    uint8_t high;

    std::string toString();
};

#endif
