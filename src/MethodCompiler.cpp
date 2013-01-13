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
#include <stdarg.h>
#include <iostream>
#include <sstream>

using namespace llvm;

Function* MethodCompiler::createFunction(TMethod* method)
{
    Type* methodParams[] = { ot.context->getPointerTo() };
    FunctionType* functionType = FunctionType::get(
        ot.object->getPointerTo(), // function return value
        methodParams,              // parameters
        false                      // we're not dealing with vararg
    );
    
    std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
    return cast<Function>( m_JITModule->getOrInsertFunction(functionName, functionType) );
}

void MethodCompiler::writePreamble(TJITContext& jit, bool isBlock)
{
    if (isBlock)
        jit.llvmContext = jit.builder->CreateBitCast(jit.llvmBlockContext, ot.context->getPointerTo());

    jit.methodPtr = jit.builder->CreateStructGEP(jit.llvmContext, 1, "method");
    
    Function* objectGetFields = m_TypeModule->getFunction("TObject::getFields()");
    
    // TODO maybe we shuld rewrite arguments[idx] using TArrayObject::getField ?

    Value* argsObjectPtr       = jit.builder->CreateStructGEP(jit.llvmContext, 2, "argObjectPtr");
    Value* argsObjectArray     = jit.builder->CreateLoad(argsObjectPtr, "argsObjectArray");
    Value* argsObject          = jit.builder->CreateBitCast(argsObjectArray, ot.object->getPointerTo(), "argsObject");
    jit.arguments              = jit.builder->CreateCall(objectGetFields, argsObject, "arguments");

    Value* methodObject        = jit.builder->CreateLoad(jit.methodPtr);
    Value* literalsObjectPtr   = jit.builder->CreateStructGEP(methodObject, 3, "literalsObjectPtr");
    Value* literalsObjectArray = jit.builder->CreateLoad(literalsObjectPtr, "literalsObjectArray");
    Value* literalsObject      = jit.builder->CreateBitCast(literalsObjectArray, ot.object->getPointerTo(), "literalsObject");
    jit.literals               = jit.builder->CreateCall(objectGetFields, literalsObject, "literals");
//    jit.literals = jit.builder->CreateBitCast(jit.literals, ot.objectArray, "literals");
    
    Value* tempsObjectPtr      = jit.builder->CreateStructGEP(jit.llvmContext, 4, "tempsObjectPtr");
    Value* tempsObjectArray    = jit.builder->CreateLoad(tempsObjectPtr, "tempsObjectArray");
    Value* tempsObject         = jit.builder->CreateBitCast(tempsObjectArray, ot.object->getPointerTo(), "tempsObject");
    jit.temporaries            = jit.builder->CreateCall(objectGetFields, tempsObject, "temporaries");
    
    Value* selfObjectPtr       = jit.builder->CreateGEP(jit.arguments, jit.builder->getInt32(0), "selfObjectPtr");
    jit.self                   = jit.builder->CreateLoad(selfObjectPtr, "self");
    jit.selfFields             = jit.builder->CreateCall(objectGetFields, jit.self, "selfFields");
}

void MethodCompiler::scanForBranches(TJITContext& jit, uint32_t byteCount /*= 0*/)
{
    // First analyzing pass. Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with
    // target bytecode offset as a key.

    TByteObject& byteCodes   = * jit.method->byteCodes;
    //uint32_t     byteCount   = byteCodes.getSize();
    uint32_t     stopPointer = jit.bytePointer + (byteCount ? byteCount : byteCodes.getSize());

    // Processing the method's bytecodes
    while (jit.bytePointer < stopPointer) {
        // Decoding the pending instruction (TODO move to a function)
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == SmalltalkVM::opExtended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[jit.bytePointer++];
        }

        if (instruction.high == SmalltalkVM::opPushBlock) {
            // Skipping the inline block instructions. 
            // They will be processed on the next pass.
            uint16_t newBytePointer = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer = newBytePointer;
            continue;
        }
        
        // We're now looking only for branch bytecodes
        if (instruction.high != SmalltalkVM::opDoSpecial)
            continue;

        switch (instruction.low) {
            case SmalltalkVM::branch:
            case SmalltalkVM::branchIfTrue:
            case SmalltalkVM::branchIfFalse: {
                // Loading branch target bytecode offset
                uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
                jit.bytePointer += 2; // skipping the branch offset data
                
                // Creating the referred basic block and inserting it into the function
                // Later it will be filled with instructions and linked to other blocks
                BasicBlock* targetBasicBlock = BasicBlock::Create(m_JITModule->getContext(), "target", jit.function);
                m_targetToBlockMap[targetOffset] = targetBasicBlock;
            } break;
        }
    }
}

Value* MethodCompiler::createArray(TJITContext& jit, uint32_t elementsCount)
{
    // Instantinating new array object
    Value* args[] = { m_globals.arrayClass, jit.builder->getInt32(elementsCount) };
    Value* arrayObject = jit.builder->CreateCall(m_RuntimeAPI.newOrdinaryObject, args);
          // arrayObject = jit.builder->CreateBitCast(arrayObject, ot.objectArray->getPointerTo());

    return arrayObject;
}

Function* MethodCompiler::compileMethod(TMethod* method)
{
    TJITContext  jit(method);
    
    // Creating the function named as "Class>>method"
    jit.function = createFunction(method);

    // First argument of every function is a pointer to TContext object
    jit.llvmContext = (Value*) (jit.function->arg_begin());
    jit.llvmContext->setName("context");

    // Creating the basic block and inserting it into the function
    BasicBlock* preamble = BasicBlock::Create(m_JITModule->getContext(), "preamble", jit.function);

    jit.builder = new IRBuilder<>(preamble);
    
    // Writing the function preamble and initializing
    // commonly used pointers such as method arguments or temporaries
    writePreamble(jit);

    
    // Switching builder context to the body's basic block
    BasicBlock* body = BasicBlock::Create(m_JITModule->getContext(), "body", jit.function);
    jit.builder->CreateBr(body);
    jit.builder->SetInsertPoint(body);

    // First analyzing pass. Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with 
    // target bytecode offset as a key.
    scanForBranches(jit);

    jit.bytePointer = 0;
    
    // Processing the method's bytecodes
    writeFunctionBody(jit);

    // TODO Write the function epilogue and do the remaining job

    return jit.function;
}

void MethodCompiler::writeFunctionBody(TJITContext& jit, uint32_t byteCount /*= 0*/)
{
    TByteObject& byteCodes   = * jit.method->byteCodes;
    uint32_t     stopPointer = jit.bytePointer + (byteCount ? byteCount : byteCodes.getSize());
    
    while (jit.bytePointer < stopPointer) {
        uint32_t currentOffset = jit.bytePointer;
        //printf("Processing offset %d / %d : ", currentOffset, stopPointer);
        
        std::map<uint32_t, llvm::BasicBlock*>::iterator iBlock = m_targetToBlockMap.find(currentOffset);
        if (iBlock != m_targetToBlockMap.end()) {
            // Somewhere in the code we have a branch instruction that
            // points to the current offset. We need to end the current
            // basic block and start a new one, linking previous
            // basic block to a new one.

            BasicBlock::iterator iInst = --jit.builder->GetInsertPoint();
            
            BasicBlock* newBlock = iBlock->second; // Picking a basic block
            if (! iInst->isTerminator())
                jit.builder->CreateBr(newBlock);       // Linking current block to a new one
            jit.builder->SetInsertPoint(newBlock); // and switching builder to a new block
        }
        
        // First of all decoding the pending instruction
        jit.instruction.low = (jit.instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        jit.instruction.high >>= 4;
        if (jit.instruction.high == SmalltalkVM::opExtended) {
            jit.instruction.high =  jit.instruction.low;
            jit.instruction.low  =  byteCodes[jit.bytePointer++];
        }

        //printOpcode(jit.instruction);

        uint32_t instCountBefore = jit.builder->GetInsertBlock()->getInstList().size();
        
        // Then writing the code
        switch (jit.instruction.high) {
            // TODO Boundary checks against container's real size
            case SmalltalkVM::opPushInstance:      doPushInstance(jit);    break;
            case SmalltalkVM::opPushArgument:      doPushArgument(jit);    break;
            case SmalltalkVM::opPushTemporary:     doPushTemporary(jit);   break;
            case SmalltalkVM::opPushLiteral:       doPushLiteral(jit);     break;
            case SmalltalkVM::opPushConstant:      doPushConstant(jit);    break;
            
            case SmalltalkVM::opPushBlock:         doPushBlock(currentOffset, jit); break;
            
            case SmalltalkVM::opAssignTemporary:   doAssignTemporary(jit); break;
            case SmalltalkVM::opAssignInstance:    doAssignInstance(jit);  break; // TODO checkRoot
            
            case SmalltalkVM::opMarkArguments:     doMarkArguments(jit);   break; // -
            case SmalltalkVM::opSendUnary:         doSendUnary(jit);       break; // -
            case SmalltalkVM::opSendBinary:        doSendBinary(jit);      break;
            case SmalltalkVM::opSendMessage:       doSendMessage(jit);     break;
            
            case SmalltalkVM::opDoSpecial:         doSpecial(jit); break;         // -
            
            case SmalltalkVM::opDoPrimitive: /* TODO */ break;
            
            default:
                fprintf(stderr, "JIT: Invalid opcode %d at offset %d in method %s\n",
                        jit.instruction.high, jit.bytePointer, jit.method->name->toString().c_str());
                //exit(1);
                //return;
        }

        uint32_t instCountAfter = jit.builder->GetInsertBlock()->getInstList().size();

        if (instCountAfter > instCountBefore)
            outs() << "[" << currentOffset << "] " << (jit.function->getName()) << ":" << (jit.builder->GetInsertBlock()->getName()) << ": " << *(--jit.builder->GetInsertPoint()) << "\n";
    }

   // printf("Done processing at offset %d\n", stopPointer);
    
}

void MethodCompiler::printOpcode(TInstruction instruction)
{
    switch (instruction.high) {
        // TODO Boundary checks against container's real size
        case SmalltalkVM::opPushInstance:    printf("doPushInstance %d\n", instruction.low);  break;
        case SmalltalkVM::opPushArgument:    printf("doPushArgument %d\n", instruction.low);  break;
        case SmalltalkVM::opPushTemporary:   printf("doPushTemporary %d\n", instruction.low); break;
        case SmalltalkVM::opPushLiteral:     printf("doPushLiteral %d\n", instruction.low);   break;
        case SmalltalkVM::opPushConstant:    printf("doPushConstant %d\n", instruction.low);  break;
        case SmalltalkVM::opPushBlock:       printf("doPushBlock %d\n", instruction.low);     break;
                                                    
        case SmalltalkVM::opAssignTemporary: printf("doAssignTemporary %d\n", instruction.low); break;
        case SmalltalkVM::opAssignInstance:  printf("doAssignInstance %d\n", instruction.low);  break; // TODO checkRoot
                                                    
        case SmalltalkVM::opMarkArguments:   printf("doMarkArguments %d\n", instruction.low); break;

        case SmalltalkVM::opSendUnary:       printf("doSendUnary\n");     break;
        case SmalltalkVM::opSendBinary:      printf("doSendBinary\n");    break;
        case SmalltalkVM::opSendMessage:     printf("doSendMessage\n");   break;
                                                    
        case SmalltalkVM::opDoSpecial:       printf("doSpecial\n"); break;
                                                    
        default:
            fprintf(stderr, "Unknown opcode %d\n", instruction.high);
    }
}

void MethodCompiler::doPushInstance(TJITContext& jit)
{
    // Self is interpreted as object array.
    // Array elements are instance variables

    uint8_t index = jit.instruction.low;
    
    Value* valuePointer      = jit.builder->CreateGEP(jit.selfFields, jit.builder->getInt32(index));
    Value* instanceVariable  = jit.builder->CreateLoad(valuePointer);
    std::string variableName = jit.method->klass->variables->getField(index)->toString();
    instanceVariable->setName(variableName);
    
    jit.pushValue(instanceVariable);
}

void MethodCompiler::doPushArgument(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;

    Value* valuePointer = jit.builder->CreateGEP(jit.arguments, jit.builder->getInt32(index));
    Value* argument     = jit.builder->CreateLoad(valuePointer);

    if (index == 0)
        argument->setName("self.");
    else {
        std::ostringstream ss;
        ss << "arg" << (uint32_t)index << ".";
        argument->setName(ss.str());
    }
    
    jit.pushValue(argument);
}

void MethodCompiler::doPushTemporary(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;

    Value* valuePointer = jit.builder->CreateGEP(jit.temporaries, jit.builder->getInt32(index));
    Value* temporary    = jit.builder->CreateLoad(valuePointer);

    std::ostringstream ss;
    ss << "temp" << (uint32_t)index << ".";
    temporary->setName(ss.str());
    
    jit.pushValue(temporary);

    outs() << "last value: " << jit.lastValue() << "\n";
}

void MethodCompiler::doPushLiteral(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;

    Value* valuePointer = jit.builder->CreateGEP(jit.literals, jit.builder->getInt32(index));
    Value* literal      = jit.builder->CreateLoad(valuePointer);

    std::ostringstream ss;
//     ss << "lit" << jit.method->literals->getField(index)->toString() << ".";
    ss << "lit" << (uint32_t)index << ".";
    literal->setName(ss.str());
    
    jit.pushValue(literal);
}

void MethodCompiler::doPushConstant(TJITContext& jit)
{
    const uint8_t constant = jit.instruction.low;
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
            Value* integerValue = jit.builder->getInt32(newInteger(constant));
            constantValue       = jit.builder->CreateIntToPtr(integerValue, ot.object);

            std::ostringstream ss;
            ss << "const" << constant << ".";
            constantValue->setName(ss.str());
        } break;
        
        case SmalltalkVM::nilConst:   constantValue = m_globals.nilObject;   break;
        case SmalltalkVM::trueConst:  constantValue = m_globals.trueObject;  break;
        case SmalltalkVM::falseConst: constantValue = m_globals.falseObject; break;
        
        default:
            fprintf(stderr, "JIT: unknown push constant %d\n", constant);
    }
    
    jit.pushValue(constantValue);
}

void MethodCompiler::doPushBlock(uint32_t currentOffset, TJITContext& jit)
{
    TByteObject& byteCodes = * jit.method->byteCodes;
    uint16_t newBytePointer = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
    jit.bytePointer += 2;

    TJITContext blockContext(jit.method);
    blockContext.bytePointer = jit.bytePointer;

    // Creating block function named Class>>method@offset
    std::ostringstream ss;
    ss << jit.function->getName().str() << "@" << currentOffset;
    std::string blockFunctionName = ss.str();

    std::vector<Type*> blockParams;
    blockParams.push_back(ot.block->getPointerTo()); // block object with context information
    
    FunctionType* blockFunctionType = FunctionType::get(
        ot.object->getPointerTo(), // block return value
        blockParams,               // parameters
        false                      // we're not dealing with vararg
    );
    blockContext.function = cast<Function>(m_JITModule->getOrInsertFunction(blockFunctionName, blockFunctionType));
    m_blockFunctions[blockFunctionName] = blockContext.function;

    // First argument of every block function is a pointer to TBlock object
    blockContext.llvmBlockContext = (Value*) (blockContext.function->arg_begin());
    blockContext.llvmBlockContext->setName("blockContext");

    uint32_t currentBytePointer = blockContext.bytePointer; // storing the position
    scanForBranches(blockContext, newBytePointer - jit.bytePointer);
    blockContext.bytePointer = currentBytePointer; // restoring after the pass
    
    // Creating the basic block and inserting it into the function
    BasicBlock* blockPreamble = BasicBlock::Create(m_JITModule->getContext(), "blockPreamble", blockContext.function);
    blockContext.builder = new IRBuilder<>(blockPreamble);
    writePreamble(blockContext, true);

    BasicBlock* blockBody = BasicBlock::Create(m_JITModule->getContext(), "blockBody", blockContext.function);
    blockContext.builder->CreateBr(blockBody);
    blockContext.builder->SetInsertPoint(blockBody);
    
    writeFunctionBody(blockContext, newBytePointer - jit.bytePointer);
    
    // Create block object and fill it with context information
//     Value* args[] = {  };
//     Value* blockObject = jit.builder->CreateCall(m_RuntimeAPI.createBlock, args);
//    jit.pushValue(blockObject);

    outs() << "block body processed\n";
    
    jit.pushValue(m_globals.nilObject); // FIXME dummy
    
    jit.bytePointer = newBytePointer;
}

void MethodCompiler::doAssignTemporary(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    Value* value  = jit.lastValue();
    
    Value* temporaryAddress = jit.builder->CreateGEP(jit.temporaries, jit.builder->getInt32(index));
    jit.builder->CreateStore(value, temporaryAddress);
}

void MethodCompiler::doAssignInstance(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    Value* value  = jit.lastValue();
    
    Value* instanceVariableAddress = jit.builder->CreateGEP(jit.selfFields, jit.builder->getInt32(index));
    jit.builder->CreateStore(value, instanceVariableAddress);
    // TODO analog of checkRoot()
}

void MethodCompiler::doMarkArguments(TJITContext& jit)
{
    // Here we need to create the arguments array from the values on the stack
    
    uint8_t argumentsCount = jit.instruction.low;

    // FIXME May be we may unroll the arguments array and pass the values directly.
    //       However, in some cases this may lead to additional architectural problems.
    Value* argumentsObject    = createArray(jit, argumentsCount);
    Function* objectGetFields = m_TypeModule->getFunction("TObject::getFields()");
    Value* argumentsFields    = jit.builder->CreateCall(objectGetFields, argumentsObject);

    // Filling object with contents
    uint8_t index = argumentsCount;
    while (index > 0) {
        Value* value = jit.popValue();
        Value* elementPtr = jit.builder->CreateGEP(argumentsFields, jit.builder->getInt32(--index));
        jit.builder->CreateStore(value, elementPtr);
    }

    Value* argumentsArray = jit.builder->CreateBitCast(argumentsObject, ot.objectArray->getPointerTo());
    
    argumentsArray->setName("margs.");
    jit.pushValue(argumentsArray);
}

void MethodCompiler::doSendUnary(TJITContext& jit)
{
    Value* value     = jit.popValue();
    Value* condition = 0;
    
    switch ((SmalltalkVM::UnaryOpcode) jit.instruction.low) {
        case SmalltalkVM::isNil:  condition = jit.builder->CreateICmpEQ(value, m_globals.nilObject, "isNil.");  break;
        case SmalltalkVM::notNil: condition = jit.builder->CreateICmpNE(value, m_globals.nilObject, "notNil."); break;
        
        default:
            fprintf(stderr, "JIT: Invalid opcode %d passed to sendUnary\n", jit.instruction.low);
    }
    
    Value* result = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
    jit.pushValue(result);
}

void MethodCompiler::doSendBinary(TJITContext& jit)
{
    // TODO Extract this code into subroutines.
    //      Replace the operation with call to LLVM function
    
    Value* rightValue = jit.popValue();
    Value* leftValue  = jit.popValue();
    
    // Checking if values are both small integers
    Function* isSmallInt  = m_TypeModule->getFunction("isSmallInteger()");
    Value*    rightIsInt  = jit.builder->CreateCall(isSmallInt, rightValue);
    Value*    leftIsInt   = jit.builder->CreateCall(isSmallInt, leftValue);
    Value*    isSmallInts = jit.builder->CreateAnd(rightIsInt, leftIsInt);
    
    BasicBlock* integersBlock   = BasicBlock::Create(m_JITModule->getContext(), "integers",   jit.function);
    BasicBlock* sendBinaryBlock = BasicBlock::Create(m_JITModule->getContext(), "sendBinary", jit.function);
    BasicBlock* resultBlock     = BasicBlock::Create(m_JITModule->getContext(), "result",     jit.function);
    
    // Dpending on the contents we may either do the integer operations
    // directly or create a send message call using operand objects
    jit.builder->CreateCondBr(isSmallInts, integersBlock, sendBinaryBlock);
    
    // Now the integers part
    jit.builder->SetInsertPoint(integersBlock);
    Function* getIntValue = m_TypeModule->getFunction("getIntegerValue()");
    Value*    rightInt    = jit.builder->CreateCall(getIntValue, rightValue);
    Value*    leftInt     = jit.builder->CreateCall(getIntValue, leftValue);
    
    Value* intResult = 0;
    switch (jit.instruction.low) {
        case 0: intResult = jit.builder->CreateICmpSLT(leftInt, rightInt); break; // operator <
        case 1: intResult = jit.builder->CreateICmpSLE(leftInt, rightInt); break; // operator <=
        case 2: intResult = jit.builder->CreateAdd(leftInt, rightInt);     break; // operator +
        default:
            fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", jit.instruction.low);
    }
    intResult->setName("int.");

    // Interpreting raw integer value as a pointer
    Function* newInteger = m_TypeModule->getFunction("newInteger()");
    Value* intAsPtr = jit.builder->CreateCall(newInteger, intResult, "intAsPtr.");
    //intResult = jit.builder->CreateIntToPtr(intResult, ot.object->getPointerTo());

    
    // Jumping out the integersBlock to the value aggregator
    jit.builder->CreateBr(resultBlock);
    
    // Now the sendBinary block
    jit.builder->SetInsertPoint(sendBinaryBlock);
    // We need to create an arguments array and fill it with argument objects
    // Then send the message just like ordinary one

    Function* objectGetFields = m_TypeModule->getFunction("TObject::getFields()");
    
    Value* argumentsObject   = createArray(jit, 2);
    Value* argFields   = jit.builder->CreateCall(objectGetFields, argumentsObject);

    Value* element0Ptr = jit.builder->CreateGEP(argFields, jit.builder->getInt32(0));
    jit.builder->CreateStore(leftValue, element0Ptr);

    Value* element1Ptr = jit.builder->CreateGEP(argFields, jit.builder->getInt32(1));
    jit.builder->CreateStore(rightValue, element1Ptr);

    Value* argumentsArray = jit.builder->CreateBitCast(argumentsObject, ot.objectArray->getPointerTo());
    // Now performing a message call
    Value*    sendMessageArgs[] = {
        jit.llvmContext, // calling context
        m_globals.binarySelectors[jit.instruction.low],
        argumentsArray
    };
    //outs() << "selector " << m_globals.binarySelectors[jit.instruction.low] << "\n";
    Value* sendMessageResult  = jit.builder->CreateCall(m_RuntimeAPI.sendMessage, sendMessageArgs);

    //Value* sendMessageResult = jit.builder->CreateCall(m_RuntimeAPI.sendMessage, arguments); 
    // Jumping out the sendBinaryBlock to the value aggregator
    jit.builder->CreateBr(resultBlock);
    
    // Now the value aggregator block
    jit.builder->SetInsertPoint(resultBlock);
    // We do not know now which way the program will be executed,
    // so we need to aggregate two possible results one of which
    // will be then selected as a return value
    PHINode* phi = jit.builder->CreatePHI(ot.object->getPointerTo(), 2);
    phi->addIncoming(intAsPtr, integersBlock);
    phi->addIncoming(sendMessageResult, sendBinaryBlock);
    
    jit.pushValue(phi);
}

void MethodCompiler::doSendMessage(TJITContext& jit)
{
    Value* arguments = jit.popValue();

    // First of all we need to get the actual message selector
    Function* getFieldFunction = m_TypeModule->getFunction("TObjectArray::getField(int)");

    Value*    literalArray     = jit.builder->CreateBitCast(jit.literals, ot.objectArray->getPointerTo());
    Value*    getFieldArgs[]   = { literalArray, jit.builder->getInt32(jit.instruction.low) };
    Value*    messageSelector  = jit.builder->CreateCall(getFieldFunction, getFieldArgs);

    messageSelector = jit.builder->CreateBitCast(messageSelector, ot.symbol->getPointerTo());
    
    std::ostringstream ss;
    ss << "#" << jit.method->literals->getField(jit.instruction.low)->toString() << ".";
    messageSelector->setName(ss.str());

    // Now performing a message call
    Value*    sendMessageArgs[] = {
        jit.llvmContext, // calling context
        messageSelector, // selector
        arguments        // message arguments
    };
    Value* result = jit.builder->CreateCall(m_RuntimeAPI.sendMessage, sendMessageArgs);
    jit.pushValue(result);
}

void MethodCompiler::doSpecial(TJITContext& jit)
{
    TByteObject& byteCodes = * jit.method->byteCodes;
    uint8_t opcode = jit.instruction.low;

//     printf("Special opcode = %d\n", opcode);
    BasicBlock::iterator iInst = jit.builder->GetInsertPoint();
    --iInst;
    
    switch (opcode) {
        case SmalltalkVM::selfReturn:
            if (! iInst->isTerminator())
                jit.builder->CreateRet(jit.self); break;
        
        case SmalltalkVM::stackReturn:
            if ( !iInst->isTerminator() && jit.hasValue() )
                jit.builder->CreateRet(jit.popValue());
            break;
            
        case SmalltalkVM::blockReturn:
            if ( !iInst->isTerminator() && jit.hasValue())
                jit.popValue();
            /* TODO unwind */
            jit.builder->CreateRet(m_globals.nilObject);
            break;
        
        case SmalltalkVM::duplicate:   jit.pushValue(jit.lastValue()); break;
        case SmalltalkVM::popTop:
            if (jit.hasValue())
                jit.popValue();
            break;

        case SmalltalkVM::branch: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer += 2; // skipping the branch offset data

            // Finding appropriate branch target
            // from the previously stored basic blocks
            BasicBlock* target = m_targetToBlockMap[targetOffset];
            jit.builder->CreateBr(target);
        } break;

        case SmalltalkVM::branchIfTrue:
        case SmalltalkVM::branchIfFalse: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer += 2; // skipping the branch offset data

            // Finding appropriate branch target
            // from the previously stored basic blocks
            BasicBlock* targetBlock = m_targetToBlockMap[targetOffset];

            // This is a block that goes right after the branch instruction.
            // If branch condition is not met execution continues right after
            BasicBlock* skipBlock = BasicBlock::Create(m_JITModule->getContext(), "branchSkip", jit.function);

            // Creating condition check
            Value* boolObject = (opcode == SmalltalkVM::branchIfTrue) ? m_globals.trueObject : m_globals.falseObject;
            Value* condition  = jit.popValue();
            Value* boolValue  = jit.builder->CreateICmpEQ(condition, boolObject);
            jit.builder->CreateCondBr(boolValue, targetBlock, skipBlock);

            // Switching to a newly created block
            jit.builder->SetInsertPoint(skipBlock);
        } break;

        case SmalltalkVM::breakpoint:
            // TODO
            break;

        default:
            printf("JIT: unknown special opcode %d", opcode);
    }
}
