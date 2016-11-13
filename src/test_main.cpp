#include <interpreter/interpreter.hpp>
#include <interpreter/special_opcodes.hpp>
#include <interpreter/usual_opcodes.hpp>
#include <interpreter/primitive_opcodes.hpp>

int main() {
    std::auto_ptr<IMemoryManager> memoryManager(new BakerMemoryManager());
    memoryManager->initializeHeap(1048576 * 1, 1048576 * 100);
    memoryManager->setLogger(std::tr1::shared_ptr<IGCLogger>(new EmptyGCLogger));
    std::auto_ptr<Image> smalltalkImage(new Image(memoryManager.get()));
    smalltalkImage->loadImage("../image/LittleSmalltalk.image");

    Interpreter::Interpreter interpreter(memoryManager.get());

    interpreter.installUsual(opcode::pushInstance, new Interpreter::PushInstanceVariable);
    interpreter.installUsual(opcode::pushArgument, new Interpreter::PushArgumentVariable);
    interpreter.installUsual(opcode::pushTemporary, new Interpreter::PushTemporaryVariable);
    interpreter.installUsual(opcode::pushLiteral, new Interpreter::PushLiteralVariable);
    interpreter.installUsual(opcode::pushConstant, new Interpreter::PushInlineConstant);
    interpreter.installUsual(opcode::assignInstance, new Interpreter::AssignInstanceVariable);
    interpreter.installUsual(opcode::assignTemporary, new Interpreter::AssignTemporaryVariable);
    interpreter.installUsual(opcode::markArguments, new Interpreter::ArrayPack);
    interpreter.installUsual(opcode::sendMessage, new Interpreter::SendMessage);
    interpreter.installUsual(opcode::sendUnary, new Interpreter::SendUnaryMessage);
    interpreter.installUsual(opcode::sendBinary, new Interpreter::SendBinaryMessage);
    interpreter.installUsual(opcode::pushBlock, new Interpreter::PushBlock);

    interpreter.installSpecial(special::selfReturn, new Interpreter::SelfReturn);
    interpreter.installSpecial(special::stackReturn, new Interpreter::StackReturn);
    interpreter.installSpecial(special::blockReturn, new Interpreter::BlockReturn);
    interpreter.installSpecial(special::duplicate, new Interpreter::Duplicate);
    interpreter.installSpecial(special::popTop, new Interpreter::PopTop);
    interpreter.installSpecial(special::branch, new Interpreter::JumpUnconditional);
    interpreter.installSpecial(special::branchIfTrue, new Interpreter::JumpIfTrue);
    interpreter.installSpecial(special::branchIfFalse, new Interpreter::JumpIfFalse);
    interpreter.installSpecial(special::sendToSuper, new Interpreter::SendToSuper);

    interpreter.installPrimitive(primitive::blockInvoke, new Interpreter::PrimitiveBlockInvoke);
    interpreter.installPrimitive(primitive::allocateObject, new Interpreter::PrimitiveAllocateObject);
    interpreter.installPrimitive(primitive::allocateByteArray, new Interpreter::PrimitiveAllocateBinaryObject);
    interpreter.installPrimitive(primitive::cloneByteObject, new Interpreter::PrimitiveCloneBinaryObject);
    interpreter.installPrimitive(primitive::objectsAreEqual, new Interpreter::PrimitiveObjectsAreEqual);
    interpreter.installPrimitive(primitive::getClass, new Interpreter::PrimitiveGetClass);
    interpreter.installPrimitive(primitive::getSize, new Interpreter::PrimitiveGetSize);
    interpreter.installPrimitive(primitive::binaryObjectAt, new Interpreter::PrimitiveBinaryObjectAt);
    interpreter.installPrimitive(primitive::binaryObjectAtPut, new Interpreter::PrimitiveBinaryObjectAtPut);
    interpreter.installPrimitive(primitive::objectAt, new Interpreter::PrimitiveObjectAt);
    interpreter.installPrimitive(primitive::objectAtPut, new Interpreter::PrimitiveObjectAtPut);
    interpreter.installPrimitive(primitive::ioGetChar, new Interpreter::PrimitiveGetChar);
    interpreter.installPrimitive(primitive::ioPutChar, new Interpreter::PrimitivePutChar);
    interpreter.installPrimitive(primitive::bulkReplace, new Interpreter::PrimitiveBulkReplace);
    interpreter.installPrimitive(primitive::startNewProcess, new Interpreter::PrimitiveStartNewProcess);
    interpreter.installPrimitive(primitive::throwError, new Interpreter::PrimitiveHalt);

    interpreter.installPrimitive(primitive::smallIntAdd, new Interpreter::PrimitiveSmallIntAdd);
    interpreter.installPrimitive(primitive::smallIntDiv, new Interpreter::PrimitiveSmallIntDiv);
    interpreter.installPrimitive(primitive::smallIntMod, new Interpreter::PrimitiveSmallIntMod);
    interpreter.installPrimitive(primitive::smallIntLess, new Interpreter::PrimitiveSmallIntLess);
    interpreter.installPrimitive(primitive::smallIntEqual, new Interpreter::PrimitiveSmallIntEqual);
    interpreter.installPrimitive(primitive::smallIntMul, new Interpreter::PrimitiveSmallIntMul);
    interpreter.installPrimitive(primitive::smallIntSub, new Interpreter::PrimitiveSmallIntSub);
    interpreter.installPrimitive(primitive::smallIntBitOr, new Interpreter::PrimitiveSmallIntBitOr);
    interpreter.installPrimitive(primitive::smallIntBitAnd, new Interpreter::PrimitiveSmallIntBitAnd);
    interpreter.installPrimitive(primitive::smallIntBitShift, new Interpreter::PrimitiveSmallIntBitShift);

    interpreter.installPrimitive(primitive::integerDiv, new Interpreter::PrimitiveIntegerDiv);
    interpreter.installPrimitive(primitive::integerMod, new Interpreter::PrimitiveIntegerMod);
    interpreter.installPrimitive(primitive::integerAdd, new Interpreter::PrimitiveIntegerAdd);
    interpreter.installPrimitive(primitive::integerMul, new Interpreter::PrimitiveIntegerMul);
    interpreter.installPrimitive(primitive::integerSub, new Interpreter::PrimitiveIntegerSub);
    interpreter.installPrimitive(primitive::integerLess, new Interpreter::PrimitiveIntegerLess);
    interpreter.installPrimitive(primitive::integerEqual, new Interpreter::PrimitiveIntegerEqual);
    interpreter.installPrimitive(primitive::integerNew, new Interpreter::PrimitiveIntegerNew);
    interpreter.installPrimitive(primitive::integerAsSmallInt, new Interpreter::PrimitiveIntegerAsSmallInt);
    interpreter.installPrimitive(primitive::integerTruncToSmallInt, new Interpreter::PrimitiveIntegerTruncateToSmallInt);
    interpreter.installPrimitive(primitive::integerAsString, new Interpreter::PrimitiveIntegerAsString);

    interpreter.installPrimitive(primitive::readLine, new Interpreter::PrimitiveReadline);
    interpreter.installPrimitive(primitive::getTimeOfDay, new Interpreter::PrimitiveGetTimeOfDay);
    interpreter.installPrimitive(primitive::getSystemTicks, new Interpreter::PrimitiveGetSystemTicks);
    interpreter.installPrimitive(primitive::collectGarbage, new Interpreter::PrimitiveCollectGarbage);

    hptr<TContext> initContext = interpreter.runtime().createHptrObject<TContext>();
    hptr<TProcess> initProcess = interpreter.runtime().createHptrObject<TProcess>();
    initProcess->context = initContext;

    initContext->arguments = interpreter.runtime().createObject<TObjectArray>(1);
    initContext->arguments->putField(0, globals.nilObject);

    initContext->bytePointer = 0;
    initContext->previousContext = static_cast<TContext*>(globals.nilObject);

    const uint32_t stackSize = globals.initialMethod->stackSize;
    initContext->stack = interpreter.runtime().createObject<TObjectArray>(stackSize);
    initContext->stackTop = 0;
    initContext->bytePointer = 0;
    initContext->method = globals.initialMethod;
    initContext->temporaries = interpreter.runtime().createObject<TObjectArray>(42);

    // And starting the image execution!
    interpreter.execute(initProcess, 0);

}
