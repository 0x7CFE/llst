#ifndef LLST_OPCODES_H_INCLUDED
#define LLST_OPCODES_H_INCLUDED

namespace opcode
{
enum Opcode {
    extended = 0,
    pushInstance,
    pushArgument,
    pushTemporary,
    pushLiteral,
    pushConstant,
    assignInstance,
    assignTemporary,
    markArguments,
    sendMessage,
    sendUnary,
    sendBinary,
    pushBlock,
    doPrimitive,
    doSpecial = 15
};
}

namespace unaryBuiltIns {
enum Opcode {
    isNil  = 0,
    notNil = 1
};
}

namespace binaryBuiltIns {
enum Operator {
    operatorLess  = 0,
    operatorLessOrEq,
    operatorPlus
};
}

namespace special
{
enum {
    selfReturn = 1,
    stackReturn,
    blockReturn,
    duplicate,
    popTop,
    branch,
    branchIfTrue,
    branchIfFalse,
    sendToSuper = 11
};
}

namespace pushConstants
{
enum {
    nil = 10,
    trueObject,
    falseObject
};
}

namespace primitive
{
enum {
    objectsAreEqual   = 1,
    getClass          = 2,
    getSize           = 4,
    inAtPut           = 5,
    startNewProcess   = 6,
    allocateObject    = 7,
    blockInvoke       = 8,
    throwError        = 19,
    allocateByteArray = 20,
    cloneByteObject   = 23,
    integerNew        = 32,
    flushCache        = 34,
    bulkReplace       = 38,
    LLVMsendMessage   = 252,
    getSystemTicks    = 253
};

enum SmallIntOpcode {
    smallIntAdd = 10,
    smallIntDiv,
    smallIntMod,
    smallIntLess,
    smallIntEqual,
    smallIntMul,
    smallIntSub,
    smallIntBitOr = 36,
    smallIntBitAnd = 37,
    smallIntBitShift = 39
};

enum {
    stringAt        = 21,
    stringAtPut     = 22,
    arrayAt         = 24,
    arrayAtPut      = 5
};

enum {
    ioGetChar = 9,
    ioPutChar = 3,
    ioFileOpen = 100,
    ioFileClose = 103,
    ioFileReadIntoByteArray = 106,
    ioFileWriteFromByteArray = 107,
    ioFileSeek = 108
};

enum IntegerOpcode {
    integerDiv = 25,
    integerMod,
    integerAdd,
    integerMul,
    integerSub,
    integerLess,
    integerEqual
};
}

#endif