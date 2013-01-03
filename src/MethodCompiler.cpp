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
    TByteObject& byteCodes   = * method->byteCodes;
    uint32_t     byteCount   = byteCodes.getSize();
    uint32_t     bytePointer = 0;
    
    StructType*  LLSTObject      = m_TypeModule->getTypeByName("struct.TObject");
    StructType*  LLSTContext     = m_TypeModule->getTypeByName("struct.TContext");
    //StructType*  LLSTMethod      = m_TypeModule->getTypeByName("struct.TMethod");
    //StructType*  LLSTSymbol      = m_TypeModule->getTypeByName("struct.TSymbol");
    //StructType*  LLSTObjectArray = m_TypeModule->getTypeByName("struct.TObjectArray");
    //StructType*  LLSTSymbolArray = m_TypeModule->getTypeByName("struct.TSymbolArray");
    
    std::vector<Type*> methodParams;
    methodParams.push_back(LLSTContext);
    
    FunctionType* methodType = FunctionType::get(
        /*result*/ LLSTObject,
        /*params*/ methodParams,
        /*varArg*/ false
    );
    
    Function* resultMethod  = cast<Function>( m_JITModule->getOrInsertFunction(method->name->toString(), methodType) );
    Value*    methodContext;
    {
        Function::arg_iterator args = resultMethod->arg_begin();
        Value* methodContext = args++;
        methodContext->setName("context");
    }
    BasicBlock* BB = BasicBlock::Create(m_JITModule->getContext(), "entry", resultMethod);
    
    llvm::IRBuilder<> builder(BB);
    Value* methodMethod = builder.CreateGEP(methodContext, builder.getInt32(1), "method");
    
    std::vector<Value*> argsIdx; // * Context.Arguments->operator[](2)
    argsIdx.push_back( builder.getInt32(2) ); // Context.Arguments*
    argsIdx.push_back( builder.getInt32(0) ); // TObject
    argsIdx.push_back( builder.getInt32(2) ); // TObject.fields *
    argsIdx.push_back( builder.getInt32(0) ); // TObject.fields
    
    Value* methodArgs   = builder.CreateGEP(methodContext, argsIdx, "args");
    
    std::vector<Value*> tmpsIdx;
    tmpsIdx.push_back( builder.getInt32(3) );
    tmpsIdx.push_back( builder.getInt32(0) );
    tmpsIdx.push_back( builder.getInt32(2) );
    tmpsIdx.push_back( builder.getInt32(0) );
    
    Value* methodTemps  = builder.CreateGEP(methodContext, tmpsIdx, "temporaries");
    Value* methodSelf   = builder.CreateGEP(methodArgs, builder.getInt32(0), "self");
    
    std::vector<Value*> stack;
    
    //TJITContext jitContext(method, getGlobalContext());

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
            case SmalltalkVM::opPushInstance: {
                Value* selfAtPtr = builder.CreateGEP(methodSelf, builder.getInt32(instruction.low));
                Value* selfAt    = builder.CreateLoad(selfAtPtr);
                stack.push_back(selfAt);
            } break;
            case SmalltalkVM::opAssignInstance: {
                Value* stackTop  = stack.back();
                Value* selfAtPtr = builder.CreateGEP(methodSelf, builder.getInt32(instruction.low));
                builder.CreateStore(&*stackTop, selfAtPtr);
            } break;
            case SmalltalkVM::opPushArgument: {
                Value* argAtPtr = builder.CreateGEP(methodArgs, builder.getInt32(instruction.low));
                Value* argAt    = builder.CreateLoad(argAtPtr);
                stack.push_back(argAt);
            } break;
            case SmalltalkVM::opAssignTemporary: {
                Value* stackTop  = stack.back();
                Value* tempAtPtr = builder.CreateGEP(methodTemps, builder.getInt32(instruction.low));
                builder.CreateStore(&*stackTop, tempAtPtr);
            } break;

            default:
                fprintf(stderr, "VM: Invalid opcode %d at offset %d in method %s",
                        instruction.high,
                        bytePointer,
                        method->name->toString().c_str());
                exit(1);
        }
    }

    // TODO Write the function epilogue and do the remaining job

    return resultMethod;
}

void MethodCompiler::doPushInstance(TJITContext& jitContext)
{
    
}
