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

Function* MethodCompiler::createFunction(TMethod* method)
{
    std::vector<Type*> methodParams;
    methodParams.push_back(ot.context->getPointerTo());

    FunctionType* functionType = FunctionType::get(
        ot.object->getPointerTo(), // function return value
        methodParams,              // parameters
        false                      // we're not dealing with vararg
    );
    
    std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
    return cast<Function>( m_JITModule->getOrInsertFunction(functionName, functionType) );
}

void MethodCompiler::writePreamble(llvm::IRBuilder<>& builder, TJITContext& context)
{
    // First argument of every function is a pointer to TContext object
    Value* contextObject = (Value*) (context.function->arg_begin());
    contextObject->setName("context");
    
    context.methodObject = builder.CreateStructGEP(contextObject, 1, "method");
    
    Function* objectGetFields = m_TypeModule->getFunction("TObject::getFields()");
    
    // TODO maybe we shuld rewrite arguments[idx] using TArrayObject::getField ?

    Value* argsObjectPtr       = builder.CreateStructGEP(contextObject, 2, "argObjectPtr");
    Value* argsObjectArray     = builder.CreateLoad(argsObjectPtr, "argsObjectArray");
    Value* argsObject          = builder.CreateBitCast(argsObjectArray, ot.object->getPointerTo());
    context.arguments          = builder.CreateCall(objectGetFields, argsObject, "arguments");
    
    Value* literalsObjectPtr   = builder.CreateStructGEP(contextObject, 3, "literalsObjectPtr");
    Value* literalsSymbolArray = builder.CreateLoad(literalsObjectPtr, "literalsSymbolArray");
    Value* literalsObject      = builder.CreateBitCast(literalsSymbolArray, ot.object->getPointerTo());
    context.literals           = builder.CreateCall(objectGetFields, literalsObject, "literals");
    
    Value* tempsObjectPtr      = builder.CreateStructGEP(contextObject, 4, "tempsObjectPtr");
    Value* tempsObjectArray    = builder.CreateLoad(tempsObjectPtr, "tempsObjectArray");
    Value* tempsObject         = builder.CreateBitCast(tempsObjectArray, ot.object->getPointerTo());
    context.temporaries        = builder.CreateCall(objectGetFields, tempsObject, "temporaries");
    
    Value* selfObjectPtr       = builder.CreateGEP(context.arguments, builder.getInt32(0), "selfObject");
    Value* selfObject          = builder.CreateLoad(selfObjectPtr, "selfObject");
    Value* selfFields          = builder.CreateStructGEP(selfObject, 2, "selfFields");
    context.self               = builder.CreateCall(objectGetFields, selfFields, "self");
}

void MethodCompiler::scanForBranches(TJITContext& jitContext)
{
    // First analyzing pass. Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with
    // target bytecode offset as a key.

    TByteObject& byteCodes   = * jitContext.method->byteCodes;
    uint32_t     byteCount   = byteCodes.getSize();

    // Processing the method's bytecodes
    while (jitContext.bytePointer < byteCount) {
        // Decoding the pending instruction (TODO move to a function)
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[jitContext.bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == SmalltalkVM::opExtended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[jitContext.bytePointer++];
        }

        // We're now looking only for branch bytecodes
        if (instruction.high != SmalltalkVM::opDoSpecial)
            continue;

        switch (instruction.low) {
            case SmalltalkVM::branch:
            case SmalltalkVM::branchIfTrue:
            case SmalltalkVM::branchIfFalse: {
                // Loading branch target bytecode offset
                uint32_t targetOffset  = byteCodes[jitContext.bytePointer] | (byteCodes[jitContext.bytePointer+1] << 8);
                jitContext.bytePointer += 2; // skipping the branch offset data
                
                // Creating the referred basic block and inserting it into the function
                // Later it will be filled with instructions and linked to other blocks
                BasicBlock* targetBasicBlock = BasicBlock::Create(m_JITModule->getContext(), "target", jitContext.function);
                m_targetToBlockMap[targetOffset] = targetBasicBlock;
            } break;
        }
    }
}

Value* MethodCompiler::createArray(IRBuilder<>& builder, uint32_t elementsCount)
{
    // Instantinating new array object
    Value* args[] = { m_globals.arrayClass, builder.getInt32(elementsCount) };
    Value* arrayObject = builder.CreateCall(m_newOrdinaryObjectFunction, args);

    return arrayObject;
}

Function* MethodCompiler::compileMethod(TMethod* method)
{
    TByteObject& byteCodes = * method->byteCodes;
    uint32_t     byteCount = byteCodes.getSize();
    
    TJITContext jitContext(method);
    
    // Creating the function named as "Class>>method"
    jitContext.function = createFunction(method);

    // Creating the basic block and inserting it into the function
    BasicBlock* currentBasicBlock = BasicBlock::Create(m_JITModule->getContext(), "preamble", jitContext.function);

    // Builder inserts instructions into basic blocks
    IRBuilder<> builder(currentBasicBlock);
    
    // Writing the function preamble and initializing
    // commonly used pointers such as method arguments or temporaries
    writePreamble(builder, jitContext);

    // First analyzing pass. Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with 
    // target bytecode offset as a key.
    scanForBranches(jitContext);
    
    // Processing the method's bytecodes
    while (jitContext.bytePointer < byteCount) {
        uint32_t currentOffset = jitContext.bytePointer;

        std::map<uint32_t, llvm::BasicBlock*>::iterator iBlock = m_targetToBlockMap.find(currentOffset);
        if (iBlock != m_targetToBlockMap.end()) {
            // Somewhere in the code we have a branch instruction that
            // points to the current offset. We need to end the current
            // basic block and start a new one, linking previous 
            // basic block to a new one.

            BasicBlock* newBlock = iBlock->second; // Picking a basic block
            builder.CreateBr(newBlock);            // Linking current block to a new one
            builder.SetInsertPoint(newBlock);      // and switching builder to a new block
        }
        
        // First of all decoding the pending instruction
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[jitContext.bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == SmalltalkVM::opExtended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[jitContext.bytePointer++];
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
                    case 9: {
                        Value* integerValue = builder.getInt32(newInteger(constant));
                        constantValue       = builder.CreateIntToPtr(integerValue, ot.object);
                    } break;

                    case SmalltalkVM::nilConst: constantValue = m_globals.nilObject;     break;
                    case SmalltalkVM::trueConst: constantValue = m_globals.trueObject;   break;
                    case SmalltalkVM::falseConst: constantValue = m_globals.falseObject; break;
                    
                    default:
                        fprintf(stderr, "JIT: unknown push constant %d\n", constant);
                }

                jitContext.pushValue(constantValue);
            } break;

            case SmalltalkVM::opPushBlock: {
                uint16_t newBytePointer = byteCodes[jitContext.bytePointer] | (byteCodes[jitContext.bytePointer+1] << 8);
                jitContext.bytePointer += 2;
                
                //Value* blockFunction = compileBlock(jitContext);
                // FIXME We need to push a block object initialized 
                //       with the IR code in additional field
                // jitContext.pushValue(blockFunction);
                jitContext.bytePointer = newBytePointer;
            } break;
            
            case SmalltalkVM::opAssignTemporary: {
                Value* value = jitContext.lastValue();
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
                Value* arguments = createArray(builder, argumentsCount);

                // Filling object with contents
                uint8_t index = argumentsCount;
                while (index > 0)
                    builder.CreateInsertValue(arguments, jitContext.popValue(), index);

                jitContext.pushValue(arguments);
            } break;

            case SmalltalkVM::opSendUnary: {
                
                Value* value     = jitContext.popValue();
                Value* condition = 0;
                
                switch ((SmalltalkVM::UnaryOpcode) instruction.low) {
                    case SmalltalkVM::isNil:  condition = builder.CreateICmpEQ(value, m_globals.nilObject); break;
                    case SmalltalkVM::notNil: condition = builder.CreateICmpNE(value, m_globals.nilObject); break;
                        
                    default:
                        fprintf(stderr, "JIT: Invalid opcode %d passed to sendUnary\n", instruction.low);
                }
                
                Value* result = builder.CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
                
                jitContext.pushValue(result);
            }; break;
            
            case SmalltalkVM::opSendBinary: {
                // TODO Extract this code into subroutines. 
                //      Replace the operation with call to LLVM function
                
                Value* rightValue = jitContext.popValue();
                Value* leftValue  = jitContext.popValue();

                // Checking if values are both small integers
                Function* isSmallInt  = m_TypeModule->getFunction("isSmallInteger()");
                Value*    rightIsInt  = builder.CreateCall(isSmallInt, rightValue);
                Value*    leftIsInt   = builder.CreateCall(isSmallInt, leftValue);
                Value*    isSmallInts = builder.CreateAnd(rightIsInt, leftIsInt);
                
                BasicBlock* integersBlock   = BasicBlock::Create(m_JITModule->getContext(), "integers",   jitContext.function);
                BasicBlock* sendBinaryBlock = BasicBlock::Create(m_JITModule->getContext(), "sendBinary", jitContext.function);
                BasicBlock* resultBlock     = BasicBlock::Create(m_JITModule->getContext(), "result",     jitContext.function);

                // Dpending on the contents we may either do the integer operations
                // directly or create a send message call using operand objects
                builder.CreateCondBr(isSmallInts, integersBlock, sendBinaryBlock);

                // Now the integers part
                builder.SetInsertPoint(integersBlock);
                Function* getIntValue = m_TypeModule->getFunction("getIntegerValue()");
                Value*    rightInt    = builder.CreateCall(getIntValue, rightValue);
                Value*    leftInt     = builder.CreateCall(getIntValue, leftValue);
                
                Value* intResult = 0;
                switch (instruction.low) {
                    case 0: intResult = builder.CreateICmpSLT(leftInt, rightInt); // operator <
                    case 1: intResult = builder.CreateICmpSLE(leftInt, rightInt); // operator <=
                    case 2: intResult = builder.CreateAdd(leftInt, rightInt);     // operator +
                    default:
                        fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", instruction.low);
                }
                // Jumping out the integersBlock to the value aggregator
                builder.CreateBr(resultBlock);

                // Now the sendBinary block
                builder.SetInsertPoint(sendBinaryBlock);
                // We need to create an arguments array and fill it with argument objects
                // Then send the message just like ordinary one
                Value* arguments = createArray(builder, 2);
                builder.CreateInsertValue(arguments, jitContext.popValue(), 0);
                builder.CreateInsertValue(arguments, jitContext.popValue(), 1);
                Value* sendMessageResult = builder.CreateCall(m_sendMessageFunction, arguments);
                // Jumping out the sendBinaryBlock to the value aggregator
                builder.CreateBr(resultBlock);
                
                // Now the value aggregator block
                builder.SetInsertPoint(resultBlock);
                // We do not know now which way the program will be executed,
                // so we need to aggregate two possible results one of which 
                // will be then selected as a return value
                PHINode* phi = builder.CreatePHI(ot.object, 2);
                phi->addIncoming(intResult, integersBlock);
                phi->addIncoming(sendMessageResult, sendBinaryBlock);
                
                jitContext.pushValue(phi);
            } break;

            case SmalltalkVM::opSendMessage: {
                Value* arguments = jitContext.popValue();
                Value* result = builder.CreateCall(m_sendMessageFunction, arguments);
                jitContext.pushValue(result);
            } break;

            case SmalltalkVM::opDoSpecial: doSpecial(instruction.low, builder, jitContext);
            
            default:
                fprintf(stderr, "JIT: Invalid opcode %d at offset %d in method %s",
                        instruction.high, jitContext.bytePointer, method->name->toString().c_str());
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
    TByteObject& byteCodes = * jitContext.method->byteCodes;
    
    switch (opcode) {
        case SmalltalkVM::selfReturn:  {
            Value* selfPtr = builder.CreateGEP(jitContext.arguments, 0);
            Value* self    = builder.CreateLoad(selfPtr);
            builder.CreateRet(self);
        } break;
        case SmalltalkVM::stackReturn: builder.CreateRet(jitContext.popValue()); break;
        case SmalltalkVM::blockReturn: /* TODO */ break;
        case SmalltalkVM::duplicate:   jitContext.pushValue(jitContext.lastValue()); break;
        case SmalltalkVM::popTop:      jitContext.popValue(); break;

        case SmalltalkVM::branch: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jitContext.bytePointer] | (byteCodes[jitContext.bytePointer+1] << 8);
            jitContext.bytePointer += 2; // skipping the branch offset data

            // Finding appropriate branch target 
            // from the previously stored basic blocks
            BasicBlock* target = m_targetToBlockMap[targetOffset];
            builder.CreateBr(target);
        } break;

        case SmalltalkVM::branchIfTrue:
        case SmalltalkVM::branchIfFalse: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jitContext.bytePointer] | (byteCodes[jitContext.bytePointer+1] << 8);
            jitContext.bytePointer += 2; // skipping the branch offset data
            
            // Finding appropriate branch target
            // from the previously stored basic blocks
            BasicBlock* targetBlock = m_targetToBlockMap[targetOffset];

            // This is a block that goes right after the branch instruction.
            // If branch condition is not met execution continues right after
            BasicBlock* skipBlock = BasicBlock::Create(m_JITModule->getContext(), "branchSkip", jitContext.function);

            // Creating condition check
            Value* boolObject = 0;
            if (opcode == SmalltalkVM::branchIfTrue) {
                boolObject = m_globals.trueObject;
            } else {
                boolObject = m_globals.falseObject;
            }
                                                                
            Value* condition = jitContext.popValue();
            Value* boolValue = builder.CreateICmpEQ(condition, boolObject);
            
            /*
            Value* boolObjectValue = builder.CreateXor(condition, boolObject); // why XOR ?!? 
                                                                               // if you want to use xor, you have to add more code:
            Value* boolIntValue    = builder.CreateBitCast(boolObjectValue, builder.getInt32Ty());
            Value* boolValue       = builder.CreateICmpEQ(boolIntValue, builder.getInt32(0));
            */
            
            builder.CreateCondBr(boolValue, targetBlock, skipBlock);

            // Switching to a newly created block
            builder.SetInsertPoint(skipBlock);
        } break;

        case SmalltalkVM::breakpoint:
            // TODO
            break;
    }
}
