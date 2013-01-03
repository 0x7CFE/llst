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

void MethodCompiler::initObjectTypes()
{
    ot.object      = m_TypeModule->getTypeByName("struct.TObject");
    ot.context     = m_TypeModule->getTypeByName("struct.TContext");
    ot.method      = m_TypeModule->getTypeByName("struct.TMethod");
    ot.symbol      = m_TypeModule->getTypeByName("struct.TSymbol");
    ot.objectArray = m_TypeModule->getTypeByName("struct.TObjectArray");
    ot.symbolArray = m_TypeModule->getTypeByName("struct.TSymbolArray");
}

Function* MethodCompiler::createFunction(TMethod* method)
{
    std::vector<Type*> methodParams;
    methodParams.push_back(ot.context);

    FunctionType* functionType = FunctionType::get(
        ot.object,    // function return value
        methodParams, // parameters
        false         // we're not dealing with vararg
    );
    
    std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
    return cast<Function>( m_JITModule->getOrInsertFunction(functionName, functionType) );
}

void MethodCompiler::writePreamble(llvm::IRBuilder<>& builder, TJITContext& context)
{
    // First argument of every function is the pointer to the TContext object
    Value* contextObject = (Value*) (context.function->arg_begin()); // FIXME is this cast correct?
    contextObject->setName("context");
    
    //Value* methodObject = builder.CreateGEP(methodContext, builder.getInt32(1), "method");
    
    std::vector<Value*> argsIdx; // * Context.Arguments->operator[](2)
    argsIdx.reserve(4);
    argsIdx.push_back( builder.getInt32(2) ); // Context.Arguments*
    argsIdx.push_back( builder.getInt32(0) ); // TObject
    argsIdx.push_back( builder.getInt32(2) ); // TObject.fields *
    argsIdx.push_back( builder.getInt32(0) ); // TObject.fields
    
    Value* methodArguments   = builder.CreateGEP(contextObject, argsIdx, "arguments");
    
    std::vector<Value*> tmpsIdx;
    tmpsIdx.reserve(4);
    tmpsIdx.push_back( builder.getInt32(3) );
    tmpsIdx.push_back( builder.getInt32(0) );
    tmpsIdx.push_back( builder.getInt32(2) );
    tmpsIdx.push_back( builder.getInt32(0) );
    
    context.temporaries = builder.CreateGEP(contextObject, tmpsIdx, "temporaries");
    context.self = builder.CreateGEP(methodArguments, builder.getInt32(0), "self");
}

Function* MethodCompiler::compileMethod(TMethod* method)
{
    TByteObject& byteCodes   = * method->byteCodes;
    uint32_t     byteCount   = byteCodes.getSize();
    uint32_t     bytePointer = 0;
    
    TJITContext jitContext(method);
    
    // Creating the function named as "Class>>method"
    jitContext.function = createFunction(method);

    // Creating the basic block and inserting it into the function
    BasicBlock* basicBlock = BasicBlock::Create(m_JITModule->getContext(), "entry", jitContext.function);

    // Builder inserts instructions into basicBlock
    llvm::IRBuilder<> builder(basicBlock);
    
    // Writing the function preamble and initializing
    // commonly used pointers such as method arguments or temporaries
    writePreamble(builder, jitContext);
    
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
                Value* selfAtPtr = builder.CreateGEP(jitContext.self, builder.getInt32(instruction.low));
                Value* selfAt    = builder.CreateLoad(selfAtPtr);
                jitContext.pushValue(selfAt);
            } break;
            
            case SmalltalkVM::opAssignInstance: {
                Value* stackTop  = jitContext.popValue();
                Value* selfAtPtr = builder.CreateGEP(jitContext.self, builder.getInt32(instruction.low));
                builder.CreateStore(stackTop, selfAtPtr);
            } break;
            
            case SmalltalkVM::opPushArgument: {
                Value* argAtPtr = builder.CreateGEP(jitContext.arguments, builder.getInt32(instruction.low));
                Value* argAt    = builder.CreateLoad(argAtPtr);
                jitContext.pushValue(argAt);
            } break;
            
            case SmalltalkVM::opAssignTemporary: {
                Value* stackTop  = jitContext.popValue();
                Value* tempAtPtr = builder.CreateGEP(jitContext.temporaries, builder.getInt32(instruction.low));
                builder.CreateStore(stackTop, tempAtPtr);
            } break;

            default:
                fprintf(stderr, "VM: Invalid opcode %d at offset %d in method %s",
                        instruction.high, bytePointer, method->name->toString().c_str());
                exit(1);
        }
    }

    // TODO Write the function epilogue and do the remaining job

    return jitContext.function;
}
