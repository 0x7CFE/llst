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

void MethodCompiler::writePreamble(TJITContext& jitContext)
{
    // First argument of every function is a pointer to TContext object
    Value* contextObject = (Value*) (jitContext.function->arg_begin());
    contextObject->setName("context");
    
    jitContext.methodObject = jitContext.builder->CreateStructGEP(contextObject, 1, "method");
    
    Function* objectGetFields = m_TypeModule->getFunction("TObject::getFields()");
    
    // TODO maybe we shuld rewrite arguments[idx] using TArrayObject::getField ?

    Value* argsObjectPtr       = jitContext.builder->CreateStructGEP(contextObject, 2, "argObjectPtr");
    Value* argsObjectArray     = jitContext.builder->CreateLoad(argsObjectPtr, "argsObjectArray");
    Value* argsObject          = jitContext.builder->CreateBitCast(argsObjectArray, ot.object->getPointerTo(), "argsObject");
    jitContext.arguments       = jitContext.builder->CreateCall(objectGetFields, argsObject, "arguments");
    
    Value* literalsObjectPtr   = jitContext.builder->CreateStructGEP(contextObject, 3, "literalsObjectPtr");
    Value* literalsObjectArray = jitContext.builder->CreateLoad(literalsObjectPtr, "literalsObjectArray");
    Value* literalsObject      = jitContext.builder->CreateBitCast(literalsObjectArray, ot.object->getPointerTo(), "literalsObject");
    jitContext.literals        = jitContext.builder->CreateCall(objectGetFields, literalsObject, "literals");
    
    Value* tempsObjectPtr      = jitContext.builder->CreateStructGEP(contextObject, 4, "tempsObjectPtr");
    Value* tempsObjectArray    = jitContext.builder->CreateLoad(tempsObjectPtr, "tempsObjectArray");
    Value* tempsObject         = jitContext.builder->CreateBitCast(tempsObjectArray, ot.object->getPointerTo(), "tempsObject");
    jitContext.temporaries     = jitContext.builder->CreateCall(objectGetFields, tempsObject, "temporaries");
    
    Value* selfObjectPtr       = jitContext.builder->CreateGEP(jitContext.arguments, jitContext.builder->getInt32(0), "selfObjectPtr");
    Value* selfObject          = jitContext.builder->CreateLoad(selfObjectPtr, "selfObject");
    jitContext.self            = jitContext.builder->CreateCall(objectGetFields, selfObject, "self");
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

Value* MethodCompiler::createArray(TJITContext& jitContext, uint32_t elementsCount)
{
    // Instantinating new array object
    Value* args[] = { m_globals.arrayClass, jitContext.builder->getInt32(elementsCount) };
    Value* arrayObject = jitContext.builder->CreateCall(m_newOrdinaryObjectFunction, args);

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

    jitContext.builder = new IRBuilder<>(currentBasicBlock);
    
    // Writing the function preamble and initializing
    // commonly used pointers such as method arguments or temporaries
    writePreamble(jitContext);

    // First analyzing pass. Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with 
    // target bytecode offset as a key.
    scanForBranches(jitContext);
    jitContext.bytePointer = 0; // TODO bytePointer != 0 if we compile Block
    
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
            jitContext.builder->CreateBr(newBlock);            // Linking current block to a new one
            jitContext.builder->SetInsertPoint(newBlock);      // and switching builder to a new block
        }
        
        // First of all decoding the pending instruction
        jitContext.instruction.low = (jitContext.instruction.high = byteCodes[jitContext.bytePointer++]) & 0x0F;
        jitContext.instruction.high >>= 4;
        if (jitContext.instruction.high == SmalltalkVM::opExtended) {
            jitContext.instruction.high =  jitContext.instruction.low;
            jitContext.instruction.low  =  byteCodes[jitContext.bytePointer++];
        }

        // Then writing the code
        switch (jitContext.instruction.high) {
            // TODO Boundary checks against container's real size
            case SmalltalkVM::opPushInstance:    doPushInstance(jitContext);    break;
            case SmalltalkVM::opPushArgument:    doPushArgument(jitContext);    break;
            case SmalltalkVM::opPushTemporary:   doPushTemporary(jitContext);   break;
            case SmalltalkVM::opPushLiteral:     doPushLiteral(jitContext);     break;
            case SmalltalkVM::opPushConstant:    doPushConstant(jitContext);    break;
            case SmalltalkVM::opPushBlock:       doPushBlock(jitContext);       break;
            
            case SmalltalkVM::opAssignTemporary: doAssignTemporary(jitContext); break;
            case SmalltalkVM::opAssignInstance:  doAssignInstance(jitContext);  break; // TODO checkRoot

            case SmalltalkVM::opMarkArguments:   doMarkArguments(jitContext);   break;
            case SmalltalkVM::opSendUnary:       doSendUnary(jitContext);       break;
            case SmalltalkVM::opSendBinary:      doSendBinary(jitContext);      break;
            case SmalltalkVM::opSendMessage:     doSendMessage(jitContext);     break;

            case SmalltalkVM::opDoSpecial:       doSpecial(jitContext.instruction.low, jitContext); break;
            
            default:
                fprintf(stderr, "JIT: Invalid opcode %d at offset %d in method %s",
                        jitContext.instruction.high, jitContext.bytePointer, method->name->toString().c_str());
                exit(1);
        }
    }

    // TODO Write the function epilogue and do the remaining job

    return jitContext.function;
}

void MethodCompiler::doPushInstance(TJITContext& jitContext)
{
    // Self is interprited as object array.
    // Array elements are instance variables
    // TODO Boundary check against self size
    Value* valuePointer     = jitContext.builder->CreateGEP(
        jitContext.self, jitContext.builder->getInt32(jitContext.instruction.low));

    Value* instanceVariable = jitContext.builder->CreateLoad(valuePointer);
    jitContext.pushValue(instanceVariable);
}

void MethodCompiler::doPushArgument(TJITContext& jitContext)
{
    // TODO Boundary check against arguments size
    Value* valuePointer = jitContext.builder->CreateGEP(
        jitContext.arguments, jitContext.builder->getInt32(jitContext.instruction.low));

    Value* argument     = jitContext.builder->CreateLoad(valuePointer);
    jitContext.pushValue(argument);
}

void MethodCompiler::doPushTemporary(TJITContext& jitContext)
{
    // TODO Boundary check against temporaries size
    Value* valuePointer = jitContext.builder->CreateGEP(
        jitContext.temporaries, jitContext.builder->getInt32(jitContext.instruction.low));

    Value* temporary    = jitContext.builder->CreateLoad(valuePointer);
    jitContext.pushValue(temporary);
}

void MethodCompiler::doPushLiteral(TJITContext& jitContext)
{
    // TODO Boundary check against literals size
    Value* valuePointer = jitContext.builder->CreateGEP(
        jitContext.literals, jitContext.builder->getInt32(jitContext.instruction.low));

    Value* literal      = jitContext.builder->CreateLoad(valuePointer);
    jitContext.pushValue(literal);
}

void MethodCompiler::doPushConstant(TJITContext& jitContext)
{
    const uint8_t constant = jitContext.instruction.low;
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
            Value* integerValue = jitContext.builder->getInt32(newInteger(constant));
            constantValue       = jitContext.builder->CreateIntToPtr(integerValue, ot.object);
        } break;
        
        case SmalltalkVM::nilConst:   constantValue = m_globals.nilObject;   break;
        case SmalltalkVM::trueConst:  constantValue = m_globals.trueObject;  break;
        case SmalltalkVM::falseConst: constantValue = m_globals.falseObject; break;
        
        default:
            fprintf(stderr, "JIT: unknown push constant %d\n", constant);
    }
    
    jitContext.pushValue(constantValue);
}

void MethodCompiler::doPushBlock(TJITContext& jitContext)
{
    TByteObject& byteCodes = * jitContext.method->byteCodes;
    uint16_t newBytePointer = byteCodes[jitContext.bytePointer] | (byteCodes[jitContext.bytePointer+1] << 8);
    jitContext.bytePointer += 2;
    
    //Value* blockFunction = compileBlock(jitContext);
    // FIXME We need to push a block object initialized
    //       with the IR code in additional field
    // jitContext.pushValue(blockFunction);
    jitContext.bytePointer = newBytePointer;
}

void MethodCompiler::doAssignTemporary(TJITContext& jitContext)
{
    Value* value = jitContext.lastValue();
    Value* temporaryAddress =
        jitContext.builder->CreateGEP(
            jitContext.temporaries,
            jitContext.builder->getInt32(jitContext.instruction.low));
    jitContext.builder->CreateStore(value, temporaryAddress);
}

void MethodCompiler::doAssignInstance(TJITContext& jitContext)
{
    Value* value = jitContext.lastValue();
    Value* instanceVariableAddress =
        jitContext.builder->CreateGEP(
            jitContext.self,
            jitContext.builder->getInt32(jitContext.instruction.low));
    jitContext.builder->CreateStore(value, instanceVariableAddress);
    // TODO analog of checkRoot()
}

void MethodCompiler::doMarkArguments(TJITContext& jitContext)
{
    // Here we need to create the arguments array from the values on the stack
    
    uint8_t argumentsCount = jitContext.instruction.low;
    
    // FIXME May be we may unroll the arguments array and pass the values directly.
    //       However, in some cases this may lead to additional architectural problems.
    Value* arguments = createArray(jitContext, argumentsCount);
    
    // Filling object with contents
    uint8_t index = argumentsCount;
    while (index > 0)
        jitContext.builder->CreateInsertValue(arguments, jitContext.popValue(), index);
    
    jitContext.pushValue(arguments);
}

void MethodCompiler::doSendUnary(TJITContext& jitContext)
{
    Value* value     = jitContext.popValue();
    Value* condition = 0;
    
    switch ((SmalltalkVM::UnaryOpcode) jitContext.instruction.low) {
        case SmalltalkVM::isNil:  condition = jitContext.builder->CreateICmpEQ(value, m_globals.nilObject); break;
        case SmalltalkVM::notNil: condition = jitContext.builder->CreateICmpNE(value, m_globals.nilObject); break;
        
        default:
            fprintf(stderr, "JIT: Invalid opcode %d passed to sendUnary\n", jitContext.instruction.low);
    }
    
    Value* result = jitContext.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
    jitContext.pushValue(result);
}

void MethodCompiler::doSendBinary(TJITContext& jitContext)
{
    // TODO Extract this code into subroutines.
    //      Replace the operation with call to LLVM function
    
    Value* rightValue = jitContext.popValue();
    Value* leftValue  = jitContext.popValue();
    
    // Checking if values are both small integers
    Function* isSmallInt  = m_TypeModule->getFunction("isSmallInteger()");
    Value*    rightIsInt  = jitContext.builder->CreateCall(isSmallInt, rightValue);
    Value*    leftIsInt   = jitContext.builder->CreateCall(isSmallInt, leftValue);
    Value*    isSmallInts = jitContext.builder->CreateAnd(rightIsInt, leftIsInt);
    
    BasicBlock* integersBlock   = BasicBlock::Create(m_JITModule->getContext(), "integers",   jitContext.function);
    BasicBlock* sendBinaryBlock = BasicBlock::Create(m_JITModule->getContext(), "sendBinary", jitContext.function);
    BasicBlock* resultBlock     = BasicBlock::Create(m_JITModule->getContext(), "result",     jitContext.function);
    
    // Dpending on the contents we may either do the integer operations
    // directly or create a send message call using operand objects
    jitContext.builder->CreateCondBr(isSmallInts, integersBlock, sendBinaryBlock);
    
    // Now the integers part
    jitContext.builder->SetInsertPoint(integersBlock);
    Function* getIntValue = m_TypeModule->getFunction("getIntegerValue()");
    Value*    rightInt    = jitContext.builder->CreateCall(getIntValue, rightValue);
    Value*    leftInt     = jitContext.builder->CreateCall(getIntValue, leftValue);
    
    Value* intResult = 0;
    switch (jitContext.instruction.low) {
        case 0: intResult = jitContext.builder->CreateICmpSLT(leftInt, rightInt); // operator <
        case 1: intResult = jitContext.builder->CreateICmpSLE(leftInt, rightInt); // operator <=
        case 2: intResult = jitContext.builder->CreateAdd(leftInt, rightInt);     // operator +
        default:
            fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", jitContext.instruction.low);
    }
    // Jumping out the integersBlock to the value aggregator
    jitContext.builder->CreateBr(resultBlock);
    
    // Now the sendBinary block
    jitContext.builder->SetInsertPoint(sendBinaryBlock);
    // We need to create an arguments array and fill it with argument objects
    // Then send the message just like ordinary one
    Value* arguments = createArray(jitContext, 2);
    jitContext.builder->CreateInsertValue(arguments, jitContext.popValue(), 0);
    jitContext.builder->CreateInsertValue(arguments, jitContext.popValue(), 1);
    Value* sendMessageResult = jitContext.builder->CreateCall(m_sendMessageFunction, arguments);
    // Jumping out the sendBinaryBlock to the value aggregator
    jitContext.builder->CreateBr(resultBlock);
    
    // Now the value aggregator block
    jitContext.builder->SetInsertPoint(resultBlock);
    // We do not know now which way the program will be executed,
    // so we need to aggregate two possible results one of which
    // will be then selected as a return value
    PHINode* phi = jitContext.builder->CreatePHI(ot.object, 2);
    phi->addIncoming(intResult, integersBlock);
    phi->addIncoming(sendMessageResult, sendBinaryBlock);
    
    jitContext.pushValue(phi);
}

void MethodCompiler::doSendMessage(TJITContext& jitContext)
{
    Value* arguments = jitContext.popValue();
    Value* result = jitContext.builder->CreateCall(m_sendMessageFunction, arguments);
    jitContext.pushValue(result);
}


Function* MethodCompiler::compileBlock(TJITContext& context)
{
    return 0; // TODO
}

void MethodCompiler::doSpecial(uint8_t opcode, TJITContext& jitContext)
{
    TByteObject& byteCodes = * jitContext.method->byteCodes;
    
    switch (opcode) {
        case SmalltalkVM::selfReturn:  {
            Value* selfPtr = jitContext.builder->CreateGEP(jitContext.arguments, 0);
            Value* self    = jitContext.builder->CreateLoad(selfPtr);
            jitContext.builder->CreateRet(self);
        } break;
        case SmalltalkVM::stackReturn: jitContext.builder->CreateRet(jitContext.popValue()); break;
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
            jitContext.builder->CreateBr(target);
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
            Value* boolObject = (opcode == SmalltalkVM::branchIfTrue) ? m_globals.trueObject : m_globals.falseObject;
            Value* condition  = jitContext.popValue();
            Value* boolValue  = jitContext.builder->CreateICmpEQ(condition, boolObject);
            jitContext.builder->CreateCondBr(boolValue, targetBlock, skipBlock);

            // Switching to a newly created block
            jitContext.builder->SetInsertPoint(skipBlock);
        } break;

        case SmalltalkVM::breakpoint:
            // TODO
            break;
    }
}
