/*
 *    opcodes.h
 *
 *    Instruction codes of the Smalltalk virtual machine
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.2
 *
 *    LLST is
 *        Copyright (C) 2012-2013 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2013 by Roman Proskuryakov <humbug@deeptown.org>
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
    ioFileSetStatIntoArray = 105,
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
