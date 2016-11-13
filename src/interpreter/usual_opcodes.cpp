#include <interpreter/usual_opcodes.hpp>
#include <interpreter/primitive_opcodes.hpp>
#include <interpreter/runtime.hpp>

#include <stdexcept>

namespace Interpreter
{

void UsualOpcodeOnlyArg::execute(Runtime& runtime, const uint8_t arg, const uint16_t /*extra*/) {
    this->execute(runtime, arg);
}

void PushInstanceVariable::execute(Runtime& runtime, const uint8_t index) {
    const TObject* const instanceVariable = runtime.getInstanceVariable(index);
    runtime.stackPush(instanceVariable);
}

void PushArgumentVariable::execute(Runtime& runtime, const uint8_t index) {
    const TObject* const argumentVariable = runtime.getArgumentVariable(index);
    runtime.stackPush(argumentVariable);
}

void PushTemporaryVariable::execute(Runtime& runtime, const uint8_t index) {
    const TObject* const temporaryVariable = runtime.getTemporaryVariable(index);
    runtime.stackPush(temporaryVariable);
}

void PushLiteralVariable::execute(Runtime& runtime, const uint8_t index) {
    const TObject* const literalVariable = runtime.getLiteralVariable(index);
    runtime.stackPush(literalVariable);
}

void PushInlineConstant::execute(Runtime& runtime, const uint8_t constant) {
    switch (constant) {
        case 0: case 1: case 2: case 3:
        case 4: case 5: case 6: case 7:
        case 8: case 9: {
            runtime.stackPush(TInteger(constant));
        } break;

        case pushConstants::nil: {
            runtime.stackPush(runtime.nilObject());
        } break;
        case pushConstants::trueObject: {
            runtime.stackPush(runtime.trueObject());
        } break;
        case pushConstants::falseObject: {
            runtime.stackPush(runtime.falseObject());
        } break;
        default: {
            std::stringstream error;
            error << "Unknown constant '" << constant << "' passed to PushInlineConstant";
            throw std::runtime_error(error.str());
        }
    }
}

void AssignTemporaryVariable::execute(Runtime& runtime, const uint8_t index) {
    TObject** temporarySlot = runtime.getTemporaryPtr(index);
    TObject* top = const_cast<TObject*>( runtime.stackTop() );
    *temporarySlot = top;
}

void AssignInstanceVariable::execute(Runtime& runtime, const uint8_t index) {
    TObject** instanceSlot = runtime.getInstancePtr(index);
    TObject* top = const_cast<TObject*>( runtime.stackTop() );
    runtime.protectSlot(top, instanceSlot);
    *instanceSlot = top;
}

void ArrayPack::execute(Runtime& runtime, const uint8_t size) {
    this->call(runtime, size);
}

void ArrayPack::call(Runtime& runtime, const uint8_t size) {
    TObjectArray* newArray = runtime.createObject<TObjectArray>(size);
    uint8_t index = size;
    while (index > 0)
        (*newArray)[--index] = runtime.stackPop();

    runtime.stackPush(newArray);
}

void SendMessage::execute(Runtime& runtime, const uint8_t index) {
    const TSymbol* const selector = static_cast<const TSymbol*>( runtime.getLiteralVariable(index) );
    this->call(runtime, selector);
}

void SendMessage::call(Runtime& runtime, const TSymbol* selector) {
    const TObjectArray* const arguments = static_cast<const TObjectArray*>( runtime.stackTop() );
    const TObject* const self = arguments->getField(0);
    const TClass* receiverClass = isSmallInteger(self) ? runtime.smallIntClass() : self->getClass();
    this->call(runtime, selector, receiverClass);
}

void SendMessage::call(Runtime& runtime, const TSymbol* selector, const TClass* receiverClass) {
    hptr<TMethod> receiverMethod = runtime.protectHptr( runtime.lookupMethod(selector, receiverClass) );
    if (!receiverMethod) {
        // Looking up the #doesNotUnderstand: method:
        receiverMethod = runtime.protectHptr( runtime.lookupMethod(runtime.badMethodSymbol(), receiverClass) );
        if (receiverMethod == 0) {
            // Something goes really wrong.
            // We can't continue the execution
            throw std::runtime_error("Could not locate #doesNotUnderstand:\n");
        }

        hptr<TSymbol> failedSelector = runtime.protectHptr( const_cast<TSymbol*>(selector) );

        // We're replacing the original call arguments with custom one
        TObjectArray* errorArguments = runtime.createObject<TObjectArray>(2);
        TObjectArray* const arguments = static_cast<TObjectArray*>( runtime.stackPop() );
        runtime.stackPush(errorArguments);

        // Filling in the failed call context information
        errorArguments->putField(0, arguments->getField(0)); // receiver object
        errorArguments->putField(1, failedSelector);         // message selector that failed
    }

    // Create a new context for the giving method and arguments
    hptr<TContext>          newContext = runtime.createHptrObject<TContext>();
    const hptr<TObjectArray>& newStack = runtime.createHptrObject<TObjectArray>(receiverMethod->stackSize);
    const hptr<TObjectArray>& newTemps = runtime.createHptrObject<TObjectArray>(receiverMethod->temporarySize);

    newContext->stack           = newStack;
    newContext->temporaries     = newTemps;
    newContext->arguments       = static_cast<TObjectArray*>( runtime.stackPop() );
    newContext->method          = receiverMethod;
    newContext->stackTop        = 0;
    newContext->bytePointer     = 0;
    newContext->previousContext = runtime.currentContext();

    runtime.setContext(newContext);
}

void SendUnaryMessage::execute(Runtime& runtime, const uint8_t opcode) {
    const TObject* const top = runtime.stackPop();
    const TObject* compareResult;

    switch (static_cast<unaryBuiltIns::Opcode>(opcode)) {
        case unaryBuiltIns::isNil : {
            compareResult = (top == runtime.nilObject()) ? runtime.trueObject() : runtime.falseObject();
        } break;
        case unaryBuiltIns::notNil : {
            compareResult = (top != runtime.nilObject()) ? runtime.trueObject() : runtime.falseObject();
        } break;

        default: {
            std::stringstream error;
            error << "Unknown opcode '" << opcode << "' passed to SendUnaryMessage";
            throw std::runtime_error(error.str());
        }
    }

    runtime.stackPush(compareResult);
}

void SendBinaryMessage::execute(Runtime& runtime, const uint8_t opcode) {
    binaryBuiltIns::Operator op = static_cast<binaryBuiltIns::Operator>(opcode);
    const TObject* rhsObject = runtime.stackTop(0);
    const TObject* lhsObject = runtime.stackTop(1);

    if (isSmallInteger(lhsObject) && isSmallInteger(rhsObject)) {
        const TObject* result = 0;
        switch (op) {
            case binaryBuiltIns::operatorLess: {
                PrimitiveSmallIntLess primitive;
                result = primitive.call(runtime, lhsObject, rhsObject);
            } break;
            case binaryBuiltIns::operatorLessOrEq: {
                PrimitiveSmallIntLessOrEq primitive;
                result = primitive.call(runtime, lhsObject, rhsObject);
            } break;
            case binaryBuiltIns::operatorPlus: {
                PrimitiveSmallIntAdd primitive;
                result = primitive.call(runtime, lhsObject, rhsObject);
            } break;
            default: {
                std::stringstream error;
                error << "Unknown operator '" << opcode << "' passed to SendBinaryMessage";
                throw std::runtime_error(error.str());
            }
        }
        runtime.stackDrop(2);
        runtime.stackPush(result);
    } else {
        const TSymbol* const selector = runtime.binaryMessages(op);

        // create array of args on the stack
        ArrayPack packer;
        packer.call(runtime, 2);

        SendMessage sender;
        sender.call(runtime, selector);
    }
}

void PushBlock::execute(Runtime& runtime, const uint8_t argumentLocation, const uint16_t pc) {
    // Block objects are usually inlined in the wrapping method code
    // pushBlock operation creates a block object initialized
    // with the proper bytecode, stack, arguments and the wrapping context.

    // Blocks are not executed directly. Instead they should be invoked
    // by sending them a 'value' method. Thus, all we need to do here is initialize
    // the block object and then skip the block body by incrementing the bytePointer
    // to the block's bytecode' size. After that bytePointer will point to the place
    // right after the block's body. There we'll probably find the actual invoking code
    // such as sendMessage to a receiver with our block as a parameter or something similar.

    // Creating block object
    hptr<TBlock> newBlock = runtime.createHptrObject<TBlock>();

    // Allocating block's stack
    const uint32_t stackSize   = runtime.currentContext()->method->stackSize;
    newBlock->stack            = runtime.createObject<TObjectArray>(stackSize);

    newBlock->argumentLocation = argumentLocation;
    newBlock->blockBytePointer = runtime.getPC();

    // We set block->bytePointer, stackTop, previousContext when block is invoked

    // Assigning creatingContext depending on the hierarchy
    // Nested blocks inherit the outer creating context
    TContext* currentContext = runtime.currentContext();
    if (runtime.currentContext()->getClass() == runtime.blockClass())
        newBlock->creatingContext = static_cast<const TBlock*>(currentContext)->creatingContext;
    else
        newBlock->creatingContext = currentContext;

    // Inheriting the context objects
    newBlock->method      = currentContext->method;
    newBlock->arguments   = currentContext->arguments;
    newBlock->temporaries = currentContext->temporaries;

    // Leaving the block object on top of the stack
    runtime.stackPush(newBlock);

    // Setting the execution point to a place right after the inlined block
    runtime.setPC(pc);
}

} // namespace
