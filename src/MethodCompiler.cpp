/*
 *    MethodCompiler.cpp
 *
 *    Implementation of MethodCompiler class which is used to
 *    translate smalltalk bytecodes to LLVM IR code
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

#include <jit.h>
#include <vm.h>

using namespace llvm;

Function* MethodCompiler::compileMethod(TMethod* method)
{
    TByteArray& byteCodes   = * method->byteCodes;
    uint32_t    byteCount   = byteCodes.getSize();
    uint32_t    bytePointer = 0;

    TJITContext jitContext(method, getGlobalContext());

    // TODO initialize llvm context data, create the function
    // Module should be initialized somewhere else
    // jitContext.module = new Module("llst", jitContext.llvmContext); 
    // jitContext.function = Function::Create();

    // TODO Write the function preamble such as init stack,
    //      temporaries and other stuff. Don't forget to
    //      mark it with @llvm.gcroot() intrinsic
    
    // Processing the method's bytecodes
    while (bytePointer < byteCount) {
        // First of all decoding the pending instruction
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == SmalltalkVM::opExtended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[bytePointer++];
        }

        // Then writing the code
        switch (instruction.high) {
            case SmalltalkVM::opPushInstance: doPushInstance(jitContext); break;
            // ...

            default:
                fprintf(stderr, "VM: Invalid opcode %d at offset %d in method %s",
                        instruction.high,
                        bytePointer,
                        method->name->toString().c_str());
                exit(1);
        }
    }

    // TODO Write the function epilogue and do the remaining job

    return jitContext.function;
}

void MethodCompiler::doPushInstance(TJITContext& jitContext)
{
    
}
