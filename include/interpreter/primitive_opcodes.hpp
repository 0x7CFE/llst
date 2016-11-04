#pragma once

#include <interpreter/opcodes.hpp>
#include <types.h>

namespace Interpreter
{

class Runtime;

class PrimitiveBase : public PrimitiveOpcode {
public:
    virtual void checkThrowArgMustBeSmallInt(const TObject* arg) const;
    virtual void checkThrowArgKindMustBe(const TObject* arg, const TClass* klass) const;
};

// First of all, executing the primitive
// If primitive succeeds then stop execution of the current method
//   and push the result onto the stack of the previous context
//
// If primitive call fails, the execution flow continues in the current method after the primitive call.
class PrimitiveOpcodeRegular : public PrimitiveBase {
public:
    virtual void execute(Runtime& runtime, const uint8_t arg);
    virtual const TObject* /*result*/ call(Runtime& runtime) = 0; // implementation of primitive
    virtual size_t consumeArgCount() = 0; // how match args does the primitive consume
    virtual void checkThrowArgCount(size_t expected, size_t provided) const;
    virtual void checkThrowStackSize(size_t expected, size_t provided) const;
};

// Some primitives deal with the context in a special way (blockInvoke, etc)
class PrimitiveOpcodeIrregular : public PrimitiveBase {
public:
    virtual void execute(Runtime& runtime, const uint8_t arg) = 0;
};

// for primitives with 0 args
class Primitive0 : public PrimitiveOpcodeRegular {
public:
    virtual size_t consumeArgCount() { return 0; }
    virtual const TObject* call(Runtime& runtime) = 0;
};
// for primitives with 1 arg
class Primitive1 : public PrimitiveOpcodeRegular {
public:
    virtual size_t consumeArgCount() { return 1; }
    virtual const TObject* call(Runtime& runtime, const TObject* arg) = 0;
    virtual const TObject* call(Runtime& runtime);
};
// for primitives with 2 args
class Primitive2 : public PrimitiveOpcodeRegular {
public:
    virtual size_t consumeArgCount() { return 2; }
    virtual const TObject* call(Runtime& runtime, const TObject* arg1, const TObject* arg2) = 0;
    virtual const TObject* call(Runtime& runtime);
};
// for primitives with 3 args
class Primitive3 : public PrimitiveOpcodeRegular {
public:
    virtual size_t consumeArgCount() { return 3; }
    virtual const TObject* call(Runtime& runtime, const TObject* const arg1, const TObject* arg2, const TObject* arg3) = 0;
    virtual const TObject* call(Runtime& runtime);
};
// for primitives with N args
class PrimitiveN : public PrimitiveOpcodeRegular {
public:
    virtual size_t consumeArgCount() = 0;
    virtual const TObject* call(Runtime& runtime, const TObjectArray* args) = 0;
    virtual const TObject* call(Runtime& runtime);
};

class PrimitiveAllocateObject : public Primitive2 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* klassObject, const TObject* sizeObject);
};
class PrimitiveAllocateBinaryObject : public Primitive2 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* klassObject, const TObject* sizeObject);
};
class PrimitiveCloneBinaryObject : public Primitive2 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* originalObject, const TObject* klassObject);
};
class PrimitiveObjectsAreEqual : public Primitive2 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* lhs, const TObject* rhs);
};
class PrimitiveGetClass : public Primitive1 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* object);
};
class PrimitiveGetSize : public Primitive1 {
public:
    virtual const TObject* call(Runtime& /*runtime*/, const TObject* object);
};
class PrimitiveBinaryObjectAt : public Primitive2 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* object, const TObject* indexObject);
};
class PrimitiveBinaryObjectAtPut : public Primitive3 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* elementObject, const TObject* object, const TObject* indexObject);
};
class PrimitiveObjectAt : public Primitive2 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* object, const TObject* indexObject);
};
class PrimitiveObjectAtPut : public Primitive3 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* elementObject, const TObject* object, const TObject* indexObject);
};
class PrimitiveGetChar : public Primitive0 {
public:
    virtual const TObject* call(Runtime& runtime);
};
class PrimitivePutChar : public Primitive1 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* arg);
};

// for SmallInt primitives
class PrimitiveSmallInt : public Primitive2 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* lhsObject, const TObject* rhsObject);
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs) = 0;
};

class PrimitiveSmallIntAdd : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntDiv : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntMod : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntLess : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntLessOrEq : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntEqual : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntMul : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntSub : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntBitOr : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntBitAnd : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveSmallIntBitShift : public PrimitiveSmallInt {
public:
    virtual const TObject* impl(Runtime& runtime, const int32_t lhs, const int32_t rhs);
};

class PrimitiveStartNewProcess : public Primitive2 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* processObject, const TObject* ticksObject);
};

class PrimitiveHalt : public PrimitiveOpcodeIrregular {
public:
    virtual void execute(Runtime& runtime, const uint8_t arg);
};

class PrimitiveBlockInvoke : public PrimitiveOpcodeIrregular {
public:
    virtual void execute(Runtime& runtime, const uint8_t arg);
};

class PrimitiveBulkReplace : public PrimitiveN {
public:
    virtual size_t consumeArgCount() { return 5; }
    virtual const TObject* call(Runtime& runtime, const TObjectArray* args);
};

class PrimitiveReadline : public Primitive1 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* promptObject);
};

class PrimitiveGetTimeOfDay : public Primitive1 {
public:
    virtual const TObject* call(Runtime& runtime, const TObject* timeValObject);
};

class PrimitiveGetSystemTicks : public Primitive0 {
public:
    virtual const TObject* call(Runtime& runtime);
};

class PrimitiveCollectGarbage : public Primitive0 {
public:
    virtual const TObject* call(Runtime& runtime);
};

} // namespace
