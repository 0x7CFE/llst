#include <interpreter/primitive_opcodes.hpp>
#include <interpreter/interpreter.hpp>
#include <interpreter/runtime.hpp>
#include <interpreter/exceptions.hpp>

#include <CompletionEngine.h>

#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <climits>
#include <iostream>

namespace Interpreter
{

const TObject* to_binary(Runtime& runtime, const ::mpz_class& value) {
    // precalculate binary_buffer size
    const size_t numb = CHAR_BIT * TLongInteger::size - TLongInteger::nails;
    const size_t count = (mpz_sizeinbase(value.get_mpz_t(), 2) + numb-1) / numb;
    size_t binary_size = count * TLongInteger::size;

    TLongInteger* integer = runtime.createObject<TLongInteger>(binary_size);
    integer->setSign( mpz_sgn(value.get_mpz_t()) );
    mpz_export(integer->getBuffer(),
               &binary_size,
               TLongInteger::order,
               TLongInteger::size,
               TLongInteger::endian,
               TLongInteger::nails,
                value.get_mpz_t());
    return integer;
}

const ::mpz_class from_binary(Runtime& runtime, const TObject* valueObject) {
    const TClass* klass = runtime.getClass(valueObject);

    if (klass == runtime.smallIntClass()) {
        return ::mpz_class( TInteger(valueObject).getValue());
    }
    if (klass == runtime.integerClass()) {
        TLongInteger* value = static_cast<TLongInteger*>(const_cast<TObject*>(valueObject));
        mpz_class result;
        mpz_import(result.get_mpz_t(),
                value->getBufferSize(),
                TLongInteger::order,
                TLongInteger::size,
                TLongInteger::endian,
                TLongInteger::nails,
                value->getBuffer());
        if (value->getSign() < 0)
            mpz_neg(result.get_mpz_t(), result.get_mpz_t());

        return result;
    }
    throw std::runtime_error("Integer from_binary accepts only TInteger or SmallInt");
}

void PrimitiveBase::checkThrowArgMustBeSmallInt(const TObject* arg) const {
    if ( !isSmallInteger(arg) ) {
        std::stringstream error;
        throw std::runtime_error("The class of the argument expected to be SmallInt, but it's not");
    }
}

void PrimitiveBase::checkThrowArgKindMustBe(const TObject* arg, const TClass* klass) const {
    if ( isSmallInteger(arg) ) {
        std::stringstream error;
        error << "The class of the argument expected to be " << klass->name->toString() << " but it is SmallInt";
        throw std::runtime_error(error.str());
    }
    // FIXME
    size_t attemps = 0;
    for (const TClass* instanceClass = arg->getClass(); attemps != 3 ; instanceClass = instanceClass->parentClass, ++attemps) {
        if (instanceClass == klass)
            return;
    }
    std::stringstream error;
    error << "The class of the argument expected to be " << klass->name->toString() << " but it is " << arg->getClass()->name->toString();
    throw std::runtime_error(error.str());
}

void PrimitiveOpcodeRegular::execute(Runtime& runtime, const uint8_t argCount) {
    // we must check the primitive was called with the right argCount
    //this->checkThrowArgCount(this->consumeArgCount(), argCount);
    // args are passed to primitive in stack
    //this->checkThrowStackSize(this->consumeArgCount(), runtime.currentContext()->stackTop);

    const TObject* result = runtime.nilObject();
    try {
        result = this->call(runtime);
    } catch(const std::bad_cast& e) {
        throw;
    } catch(const std::exception& e) {
         std::cerr << e.what() << std::endl;
        // The primitive has failed, the execution flow continues in the current method after the primitive call
        runtime.stackPush(runtime.nilObject());
        return;
    }

    // We have executed a primitive. Now we have to reject the current
    // execution context and push the result onto the previous context's stack
    const TContext* const currentContext = runtime.currentContext();
    runtime.setContext(currentContext->previousContext);
    if (runtime.currentContext() == runtime.nilObject()) {
        runtime.setProcessResult(result);
    } else {
        runtime.stackPush(result);
    }
}

void PrimitiveOpcodeRegular::checkThrowArgCount(size_t expected, size_t provided) const {
    if (provided != expected) {
        std::stringstream error;
        error << "Primitive consumes exactly " << expected << " args"
                << " but " << provided << " args were provided";
        throw std::runtime_error(error.str());
    }
}

void PrimitiveOpcodeRegular::checkThrowStackSize(size_t expected, size_t provided) const {
    if (provided < expected) {
        std::stringstream error;
        error << "Primitive expects stack to be at least " << expected << " size"
                << " but it contains only " << provided << " elements";
        throw std::runtime_error(error.str());
    }
}

const TObject* Primitive1::call(Runtime& runtime) {
    const TObject* const arg = runtime.stackPop();
    return this->call(runtime, arg);
}

const TObject* Primitive2::call(Runtime& runtime) {
    const TObject* const arg2 = runtime.stackPop();
    const TObject* const arg1 = runtime.stackPop();
    return this->call(runtime, arg1, arg2);
}

const TObject* Primitive3::call(Runtime& runtime) {
    const TObject* const arg3 = runtime.stackPop();
    const TObject* const arg2 = runtime.stackPop();
    const TObject* const arg1 = runtime.stackPop();
    return this->call(runtime, arg1, arg2, arg3);
}

const TObject* PrimitiveN::call(Runtime& runtime) {
    const uint32_t argCount = this->consumeArgCount();
    hptr<TObjectArray> args = runtime.createHptrObject<TObjectArray>(argCount);
    uint32_t i = argCount;
    while (i > 0)
        args[--i] = runtime.stackPop();
    return this->call(runtime, args);
}

const TObject* PrimitiveAllocateObject::call(Runtime& runtime, const TObject* klassObject, const TObject* sizeObject) {
    // Taking object's size and class from the stack
    const TInteger fieldsCount = sizeObject;
    const TClass* klass = static_cast<const TClass*>(klassObject);

    // Instantinating the object. Each object has size and class fields
    return runtime.newOrdinaryObject(klass, sizeof(TObject) + fieldsCount * sizeof(TObject*));
}

const TObject* PrimitiveAllocateBinaryObject::call(Runtime& runtime, const TObject* klassObject, const TObject* sizeObject) {
    const TInteger dataSize = sizeObject;
    const TClass* klass = static_cast<const TClass*>(klassObject);
    return runtime.newBinaryObject(klass, dataSize);
}

const TObject* PrimitiveCloneBinaryObject::call(Runtime& runtime, const TObject* originalObject, const TObject* klassObject) {
    const TClass* klass = static_cast<const TClass*>(klassObject);
    const hptr<TByteObject>& original = runtime.protectHptr( static_cast<TByteObject*>(const_cast<TObject*>(originalObject)) );

    // Creating clone
    uint32_t dataSize = original->getSize();
    TByteObject* const clone = runtime.newBinaryObject(klass, dataSize);

    // Cloning data
    std::memcpy(clone->getBytes(), original->getBytes(), dataSize);
    return static_cast<TObject*>(clone);
}

const TObject* PrimitiveObjectsAreEqual::call(Runtime& runtime, const TObject* lhs, const TObject* rhs) {
    return (lhs == rhs) ? runtime.trueObject() : runtime.falseObject();
}

const TObject* PrimitiveGetClass::call(Runtime& runtime, const TObject* const object) {
    return isSmallInteger(object) ? runtime.smallIntClass() : object->getClass();
}

const TObject* PrimitiveGetSize::call(Runtime& /*runtime*/, const TObject* const object) {
    const TInteger objectSize = isSmallInteger(object) ? 0 : object->getSize();
    return objectSize;
}

const TObject* PrimitiveBinaryObjectAt::call(Runtime& runtime, const TObject* object, const TObject* indexObject) {
    this->checkThrowArgMustBeSmallInt(indexObject);
    //this->checkThrowArgKindMustBe(object, runtime.stringClass());

    const TString* const binaryObject = static_cast<const TString*>(object);

    // Smalltalk indexes arrays starting from 1, not from 0
    // So we need to recalculate the actual array index before
    const uint32_t actualIndex = TInteger(indexObject) - 1;
    if (actualIndex >= binaryObject->getSize())
        throw std::runtime_error("PrimitiveBinaryObjectAt: out of bounds");
    return TInteger( binaryObject->getByte(actualIndex) );
}

const TObject* PrimitiveBinaryObjectAtPut::call(Runtime& /*runtime*/, const TObject* elementObject, const TObject* object, const TObject* indexObject) {
    this->checkThrowArgMustBeSmallInt(indexObject);
    //TODO this->checkThrowArgKindMustBe(object, runtime.stringClass());

    TString* binaryObject = static_cast<TString*>(const_cast<TObject*>(object));
    TInteger element = elementObject;

    // Smalltalk indexes arrays starting from 1, not from 0
    // So we need to recalculate the actual array index before
    uint32_t actualIndex = TInteger(indexObject) - 1;
    if (actualIndex >= binaryObject->getSize())
        throw std::runtime_error("PrimitiveBinaryObjectAtPut: out of bounds");
    binaryObject->putByte(actualIndex, element);
    return binaryObject;
}

const TObject* PrimitiveObjectAt::call(Runtime& /*runtime*/, const TObject* object, const TObject* indexObject) {
    this->checkThrowArgMustBeSmallInt(indexObject);

    const uint32_t actualIndex = TInteger(indexObject) - 1;
    if (actualIndex >= object->getSize())
        throw std::runtime_error("PrimitiveObjectAt: out of bounds");
    return object->getField(actualIndex);
}

const TObject* PrimitiveObjectAtPut::call(Runtime& runtime, const TObject* elementObject, const TObject* constObject, const TObject* indexObject) {
    this->checkThrowArgMustBeSmallInt(indexObject);

    TObject* object = const_cast<TObject*>(constObject);
    TObject* element = const_cast<TObject*>(elementObject);
    const uint32_t actualIndex = TInteger(indexObject) - 1;
    if (actualIndex >= object->getSize())
        throw std::runtime_error("PrimitiveObjectAtPut: out of bounds");

    TObject** const objectSlot = &( object->getFields()[actualIndex] );
    runtime.protectSlot(element, objectSlot);
    *objectSlot = element;

    return object;
}

const TObject* PrimitiveGetChar::call(Runtime& runtime) {
    int32_t input = std::getchar();
    if (input == EOF)
        return runtime.nilObject();
    else
        return TInteger(input);
}

const TObject* PrimitivePutChar::call(Runtime& runtime, const TObject* arg) {
    const int8_t charValue = TInteger( arg );
    std::putchar(charValue);
    return runtime.nilObject();
}

const TObject* PrimitiveSmallInt::call(Runtime& runtime, const TObject* lhsObject, const TObject* rhsObject) {
    this->checkThrowArgMustBeSmallInt(lhsObject);
    this->checkThrowArgMustBeSmallInt(rhsObject);
    const int32_t lhs = TInteger(lhsObject);
    const int32_t rhs = TInteger(rhsObject);
    return this->impl(runtime, lhs, rhs);
}

const TObject* PrimitiveSmallIntAdd::impl(Runtime& runtime, const int32_t lhs, const int32_t rhs) {
    if ( (lhs > 0 && (rhs > INT_MAX - lhs)) // `lhs + rhs` would overflow
      || (lhs < 0 && (rhs < INT_MIN - lhs)) // `lhs + rhs` would underflow
    ) {
        // TODO
        // ::mpz_class left(lhs), right(rhs);
        // return to_binary(runtime, left + right);
    }
    return TInteger(lhs + rhs);
}

const TObject* PrimitiveSmallIntDiv::impl(Runtime& /*runtime*/, const int32_t lhs, const int32_t rhs) {
    if (rhs == 0)
        throw std::runtime_error("PrimitiveSmallIntDiv: division by zero");
    if ((rhs == -1) && (lhs == INT_MIN)) { // `lhs / rhs` can overflow
        // TODO
    }
    return TInteger(lhs / rhs);
}

const TObject* PrimitiveSmallIntMod::impl(Runtime& /*runtime*/, const int32_t lhs, const int32_t rhs) {
    if (rhs == 0)
        throw std::runtime_error("PrimitiveSmallIntMod: division by zero");
    return TInteger(lhs % rhs);
}

const TObject* PrimitiveSmallIntLess::impl(Runtime& runtime, const int32_t lhs, const int32_t rhs) {
    return (lhs < rhs) ? runtime.trueObject() : runtime.falseObject();
}

const TObject* PrimitiveSmallIntLessOrEq::impl(Runtime& runtime, const int32_t lhs, const int32_t rhs) {
    return (lhs <= rhs) ? runtime.trueObject() : runtime.falseObject();
}

const TObject* PrimitiveSmallIntEqual::impl(Runtime& runtime, const int32_t lhs, const int32_t rhs) {
    return (lhs == rhs) ? runtime.trueObject() : runtime.falseObject();
}

const TObject* PrimitiveSmallIntMul::impl(Runtime& runtime, const int32_t lhs, const int32_t rhs) {
    if ( (lhs > INT_MAX / rhs) // `lhs * rhs` would overflow
      || (lhs < INT_MIN / rhs) // `lhs * rhs` would underflow
      || ((lhs == -1) && (rhs == INT_MIN)) // `lhs * rhs` can overflow
      || ((rhs == -1) && (lhs == INT_MIN)) // `lhs * rhs` can overflow
    ) {
    // TODO
        // ::mpz_class left(lhs), right(rhs);
        // return to_binary(runtime, left * right);
    }
    return TInteger(lhs * rhs);
}

const TObject* PrimitiveSmallIntSub::impl(Runtime& /*runtime*/, const int32_t lhs, const int32_t rhs) {
    if ((lhs < 0 && (rhs > INT_MAX + lhs))  // `lhs - rhs` would overflow
      ||(lhs > 0 && (rhs < INT_MIN + lhs)) // `lhs - rhs` would underflow
    ) {
        // TODO overflow
    }
    return TInteger(lhs - rhs);
}

const TObject* PrimitiveSmallIntBitOr::impl(Runtime& /*runtime*/, const int32_t lhs, const int32_t rhs) {
    return TInteger(lhs | rhs);
}

const TObject* PrimitiveSmallIntBitAnd::impl(Runtime& /*runtime*/, const int32_t lhs, const int32_t rhs) {
    return TInteger(lhs & rhs);
}

const TObject* PrimitiveSmallIntBitShift::impl(Runtime& /*runtime*/, const int32_t lhs, const int32_t rhs) {
    // operator << if rhs < 0, operator >> if rhs >= 0

    if (rhs < 0) {
        //shift right
        return TInteger(lhs >> -rhs);
    } else {
        // shift left
        int32_t result = lhs << rhs;
        if (lhs > result) {
            // catch overflow
            throw std::runtime_error("PrimitiveSmallIntBitShift: overflow");
        } else {
            return TInteger(result);
        }
    }
}

const TObject* PrimitiveInteger::call(Runtime& runtime, const TObject* lhsObject, const TObject* rhsObject) {
    //this->checkThrowArgMustBeSmallInt(lhsObject);
    //this->checkThrowArgMustBeSmallInt(rhsObject);
    const ::mpz_class& lhs = from_binary(runtime, lhsObject);
    const ::mpz_class& rhs = from_binary(runtime, rhsObject);
    return this->impl(runtime, lhs, rhs);
}

const TObject* PrimitiveIntegerDiv::impl(Runtime& runtime, const ::mpz_class& lhs, const ::mpz_class& rhs) {
    return to_binary(runtime, lhs / rhs);
}

const TObject* PrimitiveIntegerMod::impl(Runtime& runtime, const ::mpz_class& lhs, const ::mpz_class& rhs) {
    return to_binary(runtime, lhs % rhs);
}

const TObject* PrimitiveIntegerAdd::impl(Runtime& runtime, const ::mpz_class& lhs, const ::mpz_class& rhs) {
    return to_binary(runtime, lhs + rhs);
}

const TObject* PrimitiveIntegerMul::impl(Runtime& runtime, const ::mpz_class& lhs, const ::mpz_class& rhs) {
    return to_binary(runtime, lhs * rhs);
}

const TObject* PrimitiveIntegerSub::impl(Runtime& runtime, const ::mpz_class& lhs, const ::mpz_class& rhs) {
    return to_binary(runtime, lhs - rhs);
}

const TObject* PrimitiveIntegerLess::impl(Runtime& runtime, const ::mpz_class& lhs, const ::mpz_class& rhs) {
    return (mpz_cmp(lhs.get_mpz_t(), rhs.get_mpz_t()) < 0) ? runtime.trueObject() : runtime.falseObject();
}

const TObject* PrimitiveIntegerEqual::impl(Runtime& runtime, const ::mpz_class& lhs, const ::mpz_class& rhs) {
    return (mpz_cmp(lhs.get_mpz_t(), rhs.get_mpz_t()) == 0) ? runtime.trueObject() : runtime.falseObject();
}

const TObject* PrimitiveIntegerNew::call(Runtime& runtime, const TObject* arg) {
    ::mpz_class result;
    if (runtime.getClass(arg) == runtime.stringClass()) {
        const TString* input = static_cast<const TString*>(arg);
        const std::string strInput(reinterpret_cast<const char*>(input->getBytes()), input->getSize());
        result = strInput;
    } else {
        result = from_binary(runtime, arg);
    }

    return to_binary(runtime, result);
}

const TObject* PrimitiveIntegerAsSmallInt::call(Runtime& runtime, const TObject* arg) {
    const ::mpz_class& self = from_binary(runtime, arg);
    if (self.fits_sint_p()) {
        return TInteger(self.get_si());
    } else {
        throw std::runtime_error("PrimitiveIntegerAsSmallInt: the value does not fit SmallInt");
    }
}

const TObject* PrimitiveIntegerTruncateToSmallInt::call(Runtime& runtime, const TObject* arg) {
    const ::mpz_class& self = from_binary(runtime, arg);
    return TInteger(self.get_si());
}

const TObject* PrimitiveIntegerAsString::call(Runtime& runtime, const TObject* arg) {
    const ::mpz_class& self = from_binary(runtime, arg);
    const std::string& str = self.get_str();
    TString* const stringObject = runtime.createObject<TString>(str.size());

    std::memcpy(stringObject->getBytes(), str.data(), str.size());
    return stringObject;
}

/* ============== */

const TObject* PrimitiveStartNewProcess::call(Runtime& runtime, const TObject* processObject, const TObject* ticksObject) {
    this->checkThrowArgMustBeSmallInt(ticksObject);
    this->checkThrowArgKindMustBe(processObject, runtime.processClass());

    TInteger ticks = ticksObject;
    TProcess* const process = static_cast<TProcess*>(const_cast<TObject*>(processObject));

    Interpreter interpreter(runtime.interpreter());
    Interpreter::TExecuteResult result = interpreter.execute(process, ticks);

    return TInteger(result);
}

void PrimitiveHalt::execute(Runtime&, const uint8_t) {
    throw halt_execution();
}

void PrimitiveBlockInvoke::execute(Runtime& runtime, const uint8_t arg) {
    TBlock* const block = static_cast<TBlock*>(runtime.stackPop());
    uint32_t argumentLocation = block->argumentLocation;

    // Amount of arguments stored on the stack except the block itself
    uint32_t argCount = arg - 1;

    // Checking the passed temps size
    TObjectArray* const blockTemps = block->temporaries;

    if (argCount > (blockTemps ? blockTemps->getSize() - argumentLocation : 0) ) {
        runtime.stackDrop(argCount  + 1); // unrolling stack
        return; // primitive fallback
    }

    // Loading temporaries array
    for (uint32_t index = argCount - 1, count = argCount; count > 0; index--, count--)
        (*blockTemps)[argumentLocation + index] = runtime.stackPop();

    // resetting stack
    block->stackTop = 0;

    // Switching execution context to the invoking block
    block->previousContext = runtime.currentContext()->previousContext;

    // Block is bound to the method's bytecodes, so it's
    // first bytecode will not be zero but the value specified
    runtime.setContext(static_cast<TContext*>(block));
    runtime.setPC(block->blockBytePointer);
}

const TObject* PrimitiveBulkReplace::call(Runtime& /*runtime*/, const TObjectArray* args) {
    //throw std::runtime_error("PrimitiveBulkReplace: not implemented");
    TObject* destination = const_cast<TObject*>( args->getField(4) );
    const TObject* sourceStartOffset = args->getField(3);
    const TObject* source = args->getField(2);
    const TObject* destinationStopOffset = args->getField(1);
    const TObject* destinationStartOffset = args->getField(0);

    this->checkThrowArgMustBeSmallInt(sourceStartOffset);
    this->checkThrowArgMustBeSmallInt(destinationStopOffset);
    this->checkThrowArgMustBeSmallInt(destinationStartOffset);

    int32_t iSourceStartOffset      = TInteger(sourceStartOffset) - 1;
    int32_t iDestinationStartOffset = TInteger(destinationStartOffset) - 1;
    int32_t iDestinationStopOffset  = TInteger(destinationStopOffset) - 1;
    int32_t iCount                  = iDestinationStopOffset - iDestinationStartOffset + 1;

    if ( source->isBinary() && destination->isBinary() ) {
        // Interpreting pointer array as raw byte sequence
        const uint8_t* const sourceBytes = static_cast<const TByteObject*>(source)->getBytes();
        uint8_t* const destinationBytes  = static_cast<TByteObject*>(destination)->getBytes();

        // Primitive may be called on the same object, so memory overlapping may occur.
        // memmove() works much like the ordinary memcpy() except that it correctly
        // handles the case with overlapping memory areas
        std::memmove( & destinationBytes[iDestinationStartOffset], & sourceBytes[iSourceStartOffset], iCount );
    }

    if ( ! source->isBinary() && ! destination->isBinary() ) {
        TObject* const * const sourceFields      = source->getFields();
        TObject** const destinationFields = destination->getFields();

        // Primitive may be called on the same object, so memory overlapping may occur.
        // memmove() works much like the ordinary memcpy() except that it correctly
        // handles the case with overlapping memory areas
        std::memmove( & destinationFields[iDestinationStartOffset], & sourceFields[iSourceStartOffset], iCount * sizeof(TObject*) );
    }

    return destination;
}

const TObject* PrimitiveReadline::call(Runtime& runtime, const TObject* promptObject) {
    const TString* prompt = static_cast<const TString*>(promptObject);
    const std::string strPrompt(reinterpret_cast<const char*>(prompt->getBytes()), prompt->getSize());

    std::string input;
    bool userInsertedAnything = CompletionEngine::Instance()->readline(strPrompt, input);

    if ( userInsertedAnything ) {
        if ( !input.empty() )
            CompletionEngine::Instance()->addHistory(input);

        TString* const result = runtime.createObject<TString>(input.size());
        std::memcpy(result->getBytes(), input.c_str(), input.size());
        return result;
    } else
        return runtime.nilObject();
}

const TObject* PrimitiveGetTimeOfDay::call(Runtime& runtime, const TObject* timeValObject) {
    timeval result;
    int ret = gettimeofday(&result, NULL);
    if (ret) {
        std::stringstream error;
        error << "PrimitiveGetTimeOfDay failed: " << strerror(errno);
        std::runtime_error(error.str());
    }
    TObject* timeVal = const_cast<TObject*>(timeValObject);
    timeVal->putField(0, TInteger(result.tv_sec));
    timeVal->putField(1, TInteger(result.tv_usec));
    return runtime.nilObject();
}

const TObject* PrimitiveGetSystemTicks::call(Runtime& /*runtime*/) {
    timeval tv;
    int ret = gettimeofday(&tv, NULL);
    if (ret) {
        std::stringstream error;
        error << "PrimitiveGetSystemTicks failed calling gettimeofday: " << strerror(errno);
        std::runtime_error(error.str());
    }
    return TInteger( (tv.tv_sec*1000000 + tv.tv_usec) / 1000 );
}

const TObject* PrimitiveCollectGarbage::call(Runtime& runtime) {
    runtime.collectGarbage();
    return runtime.nilObject();
}

} // namespace
