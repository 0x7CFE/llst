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
    
    context.methodObject = builder.CreateGEP(contextObject, builder.getInt32(1), "method");
    context.literals     = builder.CreateGEP(context.methodObject, builder.getInt32(3), "literals");

    std::vector<Value*> argsIdx; // * Context.Arguments->operator[](2)
    argsIdx.reserve(4);
    argsIdx.push_back( builder.getInt32(2) ); // Context.Arguments*
    argsIdx.push_back( builder.getInt32(0) ); // TObject
    argsIdx.push_back( builder.getInt32(2) ); // TObject.fields *
    argsIdx.push_back( builder.getInt32(0) ); // TObject.fields
    
    context.arguments = builder.CreateGEP(contextObject, argsIdx, "arguments");
    
    std::vector<Value*> tmpsIdx;
    tmpsIdx.reserve(4);
    tmpsIdx.push_back( builder.getInt32(3) );
    tmpsIdx.push_back( builder.getInt32(0) );
    tmpsIdx.push_back( builder.getInt32(2) );
    tmpsIdx.push_back( builder.getInt32(0) );
    
    context.temporaries = builder.CreateGEP(contextObject, tmpsIdx, "temporaries");
    context.self = builder.CreateGEP(context.arguments, builder.getInt32(0), "self");
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
                // Self is interprited as object array. 
                // Array elements are instance variables
                // TODO Boundary check against self size
                Value* valuePointer     = builder.CreateGEP(jitContext.self, builder.getInt32(instruction.low));
                Value* instanceVariable = builder.CreateLoad(valuePointer);
                jitContext.pushValue(instanceVariable);
            } break;
            
            case SmalltalkVM::opPushArgument: {
                // TODO Boundary check against arguments size
                Value* valuePointer = builder.CreateGEP(jitContext.arguments, builder.getInt32(instruction.low));
                Value* argument     = builder.CreateLoad(valuePointer);
                jitContext.pushValue(argument);
            } break;
            
            case SmalltalkVM::opPushTemporary: {
                // TODO Boundary check against temporaries size
                Value* valuePointer = builder.CreateGEP(jitContext.temporaries, builder.getInt32(instruction.low));
                Value* temporary    = builder.CreateLoad(valuePointer);
                jitContext.pushValue(temporary);
            }; break;
            
            case SmalltalkVM::opPushLiteral: {
                // TODO Boundary check against literals size
                Value* valuePointer = builder.CreateGEP(jitContext.literals, builder.getInt32(instruction.low));
                Value* literal      = builder.CreateLoad(valuePointer);
                jitContext.pushValue(literal);
            } break;

            case SmalltalkVM::opPushConstant: {
                const uint8_t constant = instruction.low;
                Value* constantValue   = 0;
                switch (constant) {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9:
                        constantValue = builder.getInt32(newInteger(constant));
                        break;

                    // TODO access to global image objects such as nil, true, false, etc.
                    /* case nilConst:   stack[ec.stackTop++] = globals.nilObject;   break;
                    case trueConst:  stack[ec.stackTop++] = globals.trueObject;  break;
                    case falseConst: stack[ec.stackTop++] = globals.falseObject; break; */
                    default:
                        /* TODO unknown push constant */ ;
                        fprintf(stderr, "VM: unknown push constant %d\n", constant);
                }

                jitContext.pushValue(constantValue);
            } break;

            case SmalltalkVM::opPushBlock: {
                uint16_t newBytePointer = byteCodes[bytePointer] | (byteCodes[bytePointer+1] << 8);
                bytePointer += 2;
                
                //Value* blockFunction = compileBlock(jitContext);
                // FIXME We need to push a block object initialized 
                //       with the IR code in additional field
                // jitContext.pushValue(blockFunction);
                bytePointer = newBytePointer;
            } break;
            
            case SmalltalkVM::opAssignTemporary: {
                Value* value  = jitContext.lastValue();
                Value* temporaryAddress = builder.CreateGEP(jitContext.temporaries, builder.getInt32(instruction.low));
                builder.CreateStore(value, temporaryAddress);
            } break;

            case SmalltalkVM::opAssignInstance: {
                Value* value = jitContext.lastValue();
                Value* instanceVariableAddress = builder.CreateGEP(jitContext.self, builder.getInt32(instruction.low));
                builder.CreateStore(value, instanceVariableAddress);
                // TODO analog of checkRoot()
            } break;

            case SmalltalkVM::opMarkArguments: {
                // Here we need to create the arguments array from the values on the stack
                
                uint8_t argumentsCount = instruction.low;

                // FIXME May be we may unroll the arguments array and pass the values directly.
                //       However, in some cases this may lead to additional architectural problems.
                Value* arguments = 0; // TODO create call equivalent to newObject<TObjectArray>(argumentsCount)
                uint8_t index = argumentsCount;
                while (index > 0)
                    builder.CreateInsertValue(arguments, jitContext.popValue(), index);

                jitContext.pushValue(arguments);
            } break;

            case SmalltalkVM::opSendBinary: {
                // TODO Extract this code into subroutines. 
                //      Replace the operation with call to LLVM function
                
                Value* rightValue  = jitContext.popValue();
                Value* leftValue   = jitContext.popValue();

                // Checking if values are both small integers
                Value* rightInt    = builder.CreatePtrToInt(rightValue, Type::getInt32Ty(m_JITModule->getContext()));
                Value* leftInt     = builder.CreatePtrToInt(leftValue, Type::getInt32Ty(m_JITModule->getContext()));
                Value* andValue    = builder.CreateAnd(leftInt, rightInt);
                Value* isSmallInts = builder.CreateTrunc(andValue, Type::getInt1Ty(m_JITModule->getContext()));
                
                BasicBlock* integersBlock = BasicBlock::Create(m_JITModule->getContext(), "integers", jitContext.function);
                BasicBlock* fallbackBlock = BasicBlock::Create(m_JITModule->getContext(), "fallback", jitContext.function);

                // Here result of operation will be placed
                // TODO Rewrite using phi functions
                Value* resultPtr = builder.CreateAlloca(ot.object->getPointerTo());

                // Dpending on the contents we may either do the integer operations
                // directly or create a send message call using operand objects
                builder.CreateCondBr(isSmallInts, integersBlock, fallbackBlock);

                IRBuilder<> intBuilder(integersBlock);
                Value* intResult;
                switch (instruction.low) {
                    case 0: intResult = intBuilder.CreateICmpSLT(leftInt, rightInt); // operator <
                    case 1: intResult = intBuilder.CreateICmpSLE(leftInt, rightInt); // operator <=
                    case 2: intResult = intBuilder.CreateAdd(leftInt, rightInt);     // operator +
                    default:
                        fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", instruction.low);
                }
                // intBuilder.CreateStore(); store the intResult to the resultPtr

                // TODO Do the sendMessage call in fallbackBlock and store the result in resultPtr
                
                jitContext.pushValue(resultPtr);
            }; break;

            case SmalltalkVM::opDoSpecial: doSpecial(instruction.low, builder, jitContext);
            
            default:
                fprintf(stderr, "VM: Invalid opcode %d at offset %d in method %s",
                        instruction.high, bytePointer, method->name->toString().c_str());
                exit(1);
        }
    }

    // TODO Write the function epilogue and do the remaining job

    return jitContext.function;
}

Function* MethodCompiler::compileBlock(TJITContext& context)
{
    return 0; // TODO
}

void MethodCompiler::doSpecial(uint8_t opcode, IRBuilder<>& builder, TJITContext& jitContext)
{
    switch (opcode) {
        case SmalltalkVM::selfReturn:  builder.CreateRet(jitContext.self); break;
        case SmalltalkVM::stackReturn: builder.CreateRet(jitContext.popValue()); break;
        case SmalltalkVM::blockReturn: /* TODO */ break;
        case SmalltalkVM::duplicate:   jitContext.pushValue(jitContext.lastValue()); break;
        case SmalltalkVM::popTop:      jitContext.popValue(); break;
        
    }
}
