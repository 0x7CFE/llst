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
        jit.context = jit.builder->CreateBitCast(jit.blockContext, ot.context->getPointerTo());

    jit.methodPtr = jit.builder->CreateStructGEP(jit.context, 1, "method");

    Function* objectGetFields = m_TypeModule->getFunction("TObject::getFields()");

    // TODO maybe we shuld rewrite arguments[idx] using TArrayObject::getField ?

    Value* argsObjectPtr       = jit.builder->CreateStructGEP(jit.context, 2, "argObjectPtr");
    Value* argsObjectArray     = jit.builder->CreateLoad(argsObjectPtr, "argsObjectArray");
    Value* argsObject          = jit.builder->CreateBitCast(argsObjectArray, ot.object->getPointerTo(), "argsObject");
    jit.arguments              = jit.builder->CreateCall(objectGetFields, argsObject, "arguments");

    Value* methodObject        = jit.builder->CreateLoad(jit.methodPtr);
    Value* literalsObjectPtr   = jit.builder->CreateStructGEP(methodObject, 3, "literalsObjectPtr");
    Value* literalsObjectArray = jit.builder->CreateLoad(literalsObjectPtr, "literalsObjectArray");
    Value* literalsObject      = jit.builder->CreateBitCast(literalsObjectArray, ot.object->getPointerTo(), "literalsObject");
    jit.literals               = jit.builder->CreateCall(objectGetFields, literalsObject, "literals");

    Value* tempsObjectPtr      = jit.builder->CreateStructGEP(jit.context, 4, "tempsObjectPtr");
    Value* tempsObjectArray    = jit.builder->CreateLoad(tempsObjectPtr, "tempsObjectArray");
    Value* tempsObject         = jit.builder->CreateBitCast(tempsObjectArray, ot.object->getPointerTo(), "tempsObject");
    jit.temporaries            = jit.builder->CreateCall(objectGetFields, tempsObject, "temporaries");

    Value* selfObjectPtr       = jit.builder->CreateGEP(jit.arguments, jit.builder->getInt32(0), "selfObjectPtr");
    jit.self                   = jit.builder->CreateLoad(selfObjectPtr, "self");
    jit.selfFields             = jit.builder->CreateCall(objectGetFields, jit.self, "selfFields");
}

bool MethodCompiler::scanForBlockReturn(TJITContext& jit, uint32_t byteCount/* = 0*/)
{
    uint32_t previousBytePointer = jit.bytePointer;

    TByteObject& byteCodes   = * jit.method->byteCodes;
    uint32_t     stopPointer = jit.bytePointer + (byteCount ? byteCount : byteCodes.getSize());

    // Processing the method's bytecodes
    while (jit.bytePointer < stopPointer) {
        //uint32_t currentOffset = jit.bytePointer;
        //printf("scanForBlockReturn: Processing offset %d / %d \n", currentOffset, stopPointer);

        // Decoding the pending instruction (TODO move to a function)
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == SmalltalkVM::opExtended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[jit.bytePointer++];
        }

        if (instruction.high == SmalltalkVM::opPushBlock) {
            uint16_t newBytePointer = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer += 2;

            // Recursively processing the nested block
            if (scanForBlockReturn(jit, newBytePointer - jit.bytePointer)) {
                // Resetting bytePointer to an old value
                jit.bytePointer = previousBytePointer;
                return true;
            }

            // Skipping block's bytecodes
            jit.bytePointer = newBytePointer;
        }

        // We're now looking only for branch bytecodes
        if (instruction.high != SmalltalkVM::opDoSpecial)
            continue;

        switch (instruction.low) {
            case SmalltalkVM::blockReturn:
                // outs() << "Found a block return at offset " << currentOffset << "\n";

                // Resetting bytePointer to an old value
                jit.bytePointer = previousBytePointer;
                return true;

            case SmalltalkVM::branch:
            case SmalltalkVM::branchIfFalse:
            case SmalltalkVM::branchIfTrue:
                //uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
                jit.bytePointer += 2; // skipping the branch offset data
                continue;
        }
    }

    // Resetting bytePointer to an old value
    jit.bytePointer = previousBytePointer;
    return false;
}

void MethodCompiler::scanForBranches(TJITContext& jit, uint32_t byteCount /*= 0*/)
{
    // First analyzing pass. Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with
    // target bytecode offset as a key.

    uint32_t previousBytePointer = jit.bytePointer;

    TByteObject& byteCodes   = * jit.method->byteCodes;
    uint32_t     stopPointer = jit.bytePointer + (byteCount ? byteCount : byteCodes.getSize());

    // Processing the method's bytecodes
    while (jit.bytePointer < stopPointer) {
        // uint32_t currentOffset = jit.bytePointer;
        // printf("scanForBranches: Processing offset %d / %d \n", currentOffset, stopPointer);

        // Decoding the pending instruction (TODO move to a function)
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == SmalltalkVM::opExtended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[jit.bytePointer++];
        }

        if (instruction.high == SmalltalkVM::opPushBlock) {
            // Skipping the nested block's bytecodes
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
                BasicBlock* targetBasicBlock = BasicBlock::Create(m_JITModule->getContext(), "branch.", jit.function);
                m_targetToBlockMap[targetOffset] = targetBasicBlock;

                // outs() << "Branch site: " << currentOffset << " -> " << targetOffset << " (" << targetBasicBlock->getName() << ")\n";
            } break;
        }
    }

    // Resetting bytePointer to an old value
    jit.bytePointer = previousBytePointer;
}

Value* MethodCompiler::createArray(TJITContext& jit, uint32_t elementsCount)
{
    // Instantinating new array object
    uint32_t slotSize = sizeof(TObject) + elementsCount * sizeof(TObject*);
    Value* args[] = { m_globals.arrayClass, jit.builder->getInt32(slotSize) };
    Value* arrayObject = jit.builder->CreateCall(m_runtimeAPI.newOrdinaryObject, args);

    return arrayObject;
}

Function* MethodCompiler::compileMethod(TMethod* method)
{
    TJITContext  jit(method);

    // Creating the function named as "Class>>method"
    jit.function = createFunction(method);

    // First argument of every function is a pointer to TContext object
    jit.context = (Value*) (jit.function->arg_begin());
    jit.context->setName("context");

    // Creating the preamble basic block and inserting it into the function
    // It will contain basic initialization code (args, temps and so on)
    BasicBlock* preamble = BasicBlock::Create(m_JITModule->getContext(), "preamble", jit.function);

    // Creating the instruction builder
    jit.builder = new IRBuilder<>(preamble);

    // Checking whether method contains inline blocks that has blockReturn instruction.
    // If this is true we need to put an exception handler into the method and treat
    // all send message operations as invokes, not just simple calls
    jit.methodHasBlockReturn = scanForBlockReturn(jit);

    // Writing the function preamble and initializing
    // commonly used pointers such as method arguments or temporaries
    writePreamble(jit);

    // Writing exception handlers for the
    // correct operation of block return
    if (jit.methodHasBlockReturn)
        writeLandingPad(jit);

    // Switching builder context to the body's basic block from the preamble
    BasicBlock* body = BasicBlock::Create(m_JITModule->getContext(), "body", jit.function);
    jit.builder->SetInsertPoint(preamble);
    jit.builder->CreateBr(body);

    // Resetting the builder to the body
    jit.builder->SetInsertPoint(body);

    // Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with
    // target bytecode offset as a key.
    scanForBranches(jit);

    // Processing the method's bytecodes
    writeFunctionBody(jit);

    // Cleaning up
    m_blockFunctions.clear();
    m_targetToBlockMap.clear();

    return jit.function;
}

void MethodCompiler::writeFunctionBody(TJITContext& jit, uint32_t byteCount /*= 0*/)
{
    TByteObject& byteCodes   = * jit.method->byteCodes;
    uint32_t     stopPointer = jit.bytePointer + (byteCount ? byteCount : byteCodes.getSize());

    while (jit.bytePointer < stopPointer) {
        uint32_t currentOffset = jit.bytePointer;
        // printf("Processing offset %d / %d : ", currentOffset, stopPointer);

        std::map<uint32_t, llvm::BasicBlock*>::iterator iBlock = m_targetToBlockMap.find(currentOffset);
        if (iBlock != m_targetToBlockMap.end()) {
            // Somewhere in the code we have a branch instruction that
            // points to the current offset. We need to end the current
            // basic block and start a new one, linking previous
            // basic block to a new one.

            BasicBlock* newBlock = iBlock->second; // Picking a basic block
            BasicBlock::iterator iInst = jit.builder->GetInsertPoint();

            if (iInst != jit.builder->GetInsertBlock()->begin())
                --iInst;

            outs() << "Prev is: " << *newBlock << "\n";
            if (! iInst->isTerminator())
                outs() << *jit.builder->CreateBr(newBlock) << "\n"; // Linking current block to a new one

            jit.builder->SetInsertPoint(newBlock); // and switching builder to a new block
        }

        // First of all decoding the pending instruction
        jit.instruction.low = (jit.instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        jit.instruction.high >>= 4;
        if (jit.instruction.high == SmalltalkVM::opExtended) {
            jit.instruction.high =  jit.instruction.low;
            jit.instruction.low  =  byteCodes[jit.bytePointer++];
        }

        printOpcode(jit.instruction);

//         uint32_t instCountBefore = jit.builder->GetInsertBlock()->getInstList().size();

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
            case SmalltalkVM::opAssignInstance:    doAssignInstance(jit);  break;

            case SmalltalkVM::opMarkArguments:     doMarkArguments(jit);   break;
            case SmalltalkVM::opSendUnary:         doSendUnary(jit);       break;
            case SmalltalkVM::opSendBinary:        doSendBinary(jit);      break;
            case SmalltalkVM::opSendMessage:       doSendMessage(jit);     break;

            case SmalltalkVM::opDoSpecial:         doSpecial(jit); break;
            case SmalltalkVM::opDoPrimitive: 		doPrimitive(jit); break;

            default:
                fprintf(stderr, "JIT: Invalid opcode %d at offset %d in method %s\n",
                        jit.instruction.high, jit.bytePointer, jit.method->name->toString().c_str());
        }

//         uint32_t instCountAfter = jit.builder->GetInsertBlock()->getInstList().size();

//            if (instCountAfter > instCountBefore)
//                outs() << "[" << currentOffset << "] " << (jit.function->getName()) << ":" << (jit.builder->GetInsertBlock()->getName()) << ": " << *(--jit.builder->GetInsertPoint()) << "\n";
    }
}

void MethodCompiler::writeLandingPad(TJITContext& jit)
{
    outs() << "Writing landing pad\n";

    jit.exceptionLandingPad = BasicBlock::Create(m_JITModule->getContext(), "landingPad", jit.function);
    jit.builder->SetInsertPoint(jit.exceptionLandingPad);

    Value* gxx_personality_i8 = jit.builder->CreateBitCast(m_exceptionAPI.gxx_personality, jit.builder->getInt8PtrTy());
    Type* caughtType = StructType::get(jit.builder->getInt8PtrTy(), jit.builder->getInt32Ty(), NULL);

    LandingPadInst* caughtResult = jit.builder->CreateLandingPad(caughtType, gxx_personality_i8, 1);
    caughtResult->addClause(m_exceptionAPI.blockReturnType);

    Value* thrownException  = jit.builder->CreateExtractValue(caughtResult, 0);
    Value* exceptionObject  = jit.builder->CreateCall(m_exceptionAPI.cxa_begin_catch, thrownException);
    Value* blockResult      = jit.builder->CreateBitCast(exceptionObject, ot.blockReturn->getPointerTo());

    Value* returnValuePtr   = jit.builder->CreateStructGEP(blockResult, 0);
    Value* returnValue      = jit.builder->CreateLoad(returnValuePtr);

    Value* targetContextPtr = jit.builder->CreateStructGEP(blockResult, 1);
    Value* targetContext    = jit.builder->CreateLoad(targetContextPtr);

    jit.builder->CreateCall(m_exceptionAPI.cxa_end_catch);

    BasicBlock* returnBlock  = BasicBlock::Create(m_JITModule->getContext(), "return",  jit.function);
    BasicBlock* rethrowBlock = BasicBlock::Create(m_JITModule->getContext(), "rethrow", jit.function);

    Value* compareTargets = jit.builder->CreateICmpEQ(jit.context, targetContext);
    jit.builder->CreateCondBr(compareTargets, returnBlock, rethrowBlock);

    jit.builder->SetInsertPoint(returnBlock);
    jit.builder->CreateRet(returnValue);

    jit.builder->SetInsertPoint(rethrowBlock);
    jit.builder->CreateResume(caughtResult);
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

        case SmalltalkVM::opDoSpecial:       printf("doSpecial\n");       break;
        case SmalltalkVM::opDoPrimitive:     printf("doPrimitive\n");     break;

        default:
            fprintf(stderr, "JIT: Unknown opcode %d\n", instruction.high);
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

    if (index == 0) {
        jit.pushValue(jit.self);
    } else {
        Value* valuePointer = jit.builder->CreateGEP(jit.arguments, jit.builder->getInt32(index));
        Value* argument     = jit.builder->CreateLoad(valuePointer);

        std::ostringstream ss;
        ss << "arg" << (uint32_t)index << ".";
        argument->setName(ss.str());

        jit.pushValue(argument);
    }
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
}

void MethodCompiler::doPushLiteral(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    Value* literal = 0; // here will be the value

    // Checking whether requested literal is a small integer value.
    // If this is true just pushing the immediate constant value instead
    TObject* literalObject = jit.method->literals->getField(index);
    if (isSmallInteger(literalObject)) {
        Value* constant = jit.builder->getInt32(reinterpret_cast<uint32_t>(literalObject));
        literal = jit.builder->CreateIntToPtr(constant, ot.object->getPointerTo());
    } else {
        Value* valuePointer = jit.builder->CreateGEP(jit.literals, jit.builder->getInt32(index));
        literal = jit.builder->CreateLoad(valuePointer);
    }

    std::ostringstream ss;
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
            Value* integerValue = jit.builder->getInt32(newInteger((uint32_t)constant));
            constantValue       = jit.builder->CreateIntToPtr(integerValue, ot.object->getPointerTo());

            std::ostringstream ss;
            ss << "const" << (uint32_t) constant << ".";
            constantValue->setName(ss.str());
        } break;

        case SmalltalkVM::nilConst:   outs() << "nil ";   constantValue = m_globals.nilObject;   break;
        case SmalltalkVM::trueConst:  outs() << "true ";  constantValue = m_globals.trueObject;  break;
        case SmalltalkVM::falseConst: outs() << "false "; constantValue = m_globals.falseObject; break;

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
    const uint16_t blockOffset = jit.bytePointer;
    std::ostringstream ss;
    ss << jit.function->getName().str() << "@" << blockOffset; //currentOffset;
    std::string blockFunctionName = ss.str();

    outs() << "Creating block function "  << blockFunctionName << "\n";

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
    blockContext.blockContext = (Value*) (blockContext.function->arg_begin());
    blockContext.blockContext->setName("blockContext");

    // Creating the basic block and inserting it into the function
    BasicBlock* blockPreamble = BasicBlock::Create(m_JITModule->getContext(), "blockPreamble", blockContext.function);
    blockContext.builder = new IRBuilder<>(blockPreamble);
    writePreamble(blockContext, true);
    scanForBranches(blockContext, newBytePointer - jit.bytePointer);

    BasicBlock* blockBody = BasicBlock::Create(m_JITModule->getContext(), "blockBody", blockContext.function);
    blockContext.builder->CreateBr(blockBody);
    blockContext.builder->SetInsertPoint(blockBody);

    writeFunctionBody(blockContext, newBytePointer - jit.bytePointer);

    // Create block object and fill it with context information
    Value* args[] = {
        jit.context,                               // creatingContext
        jit.builder->getInt8(jit.instruction.low), // arg offset
        jit.builder->getInt16(blockOffset)         // bytePointer
    };
    Value* blockObject = jit.builder->CreateCall(m_runtimeAPI.createBlock, args);
    blockObject = jit.builder->CreateBitCast(blockObject, ot.object->getPointerTo());
    blockObject->setName("block.");
    jit.pushValue(blockObject);

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
    jit.builder->CreateCall2(m_runtimeAPI.checkRoot, value, instanceVariableAddress);
}

void MethodCompiler::doMarkArguments(TJITContext& jit)
{
    // Here we need to create the arguments array from the values on the stack
    uint8_t argumentsCount = jit.instruction.low;

    // FIXME Probably we may unroll the arguments array and pass the values directly.
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
    // 0, 1 or 2 for '<', '<=' or '+' respectively
    uint8_t opcode = jit.instruction.low;

    Value* rightValue = jit.popValue();
    Value* leftValue  = jit.popValue();

    // Checking if values are both small integers
    Function* isSmallInt  = m_TypeModule->getFunction("isSmallInteger()");
    Value*    rightIsInt  = jit.builder->CreateCall(isSmallInt, rightValue);
    Value*    leftIsInt   = jit.builder->CreateCall(isSmallInt, leftValue);
    Value*    isSmallInts = jit.builder->CreateAnd(rightIsInt, leftIsInt);

    BasicBlock* integersBlock   = BasicBlock::Create(m_JITModule->getContext(), "asIntegers.", jit.function);
    BasicBlock* sendBinaryBlock = BasicBlock::Create(m_JITModule->getContext(), "asObjects.",  jit.function);
    BasicBlock* resultBlock     = BasicBlock::Create(m_JITModule->getContext(), "result.",     jit.function);

    // Dpending on the contents we may either do the integer operations
    // directly or create a send message call using operand objects
    jit.builder->CreateCondBr(isSmallInts, integersBlock, sendBinaryBlock);

    // Now the integers part
    jit.builder->SetInsertPoint(integersBlock);
    Function* getIntValue  = m_TypeModule->getFunction("getIntegerValue()");
    Value*    rightInt     = jit.builder->CreateCall(getIntValue, rightValue);
    Value*    leftInt      = jit.builder->CreateCall(getIntValue, leftValue);

    Value* intResult       = 0;  // this will be an immediate operation result
    Value* intResultObject = 0; // this will be actual object to return
    switch (opcode) {
        case 0: intResult = jit.builder->CreateICmpSLT(leftInt, rightInt); break; // operator <
        case 1: intResult = jit.builder->CreateICmpSLE(leftInt, rightInt); break; // operator <=
        case 2: intResult = jit.builder->CreateAdd(leftInt, rightInt);     break; // operator +
        default:
            fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", opcode);
    }

    // Checking which operation was performed and
    // processing the intResult object in the proper way
    if (opcode == 2) {
        // Result of + operation will be number.
        // We need to create TInteger value and cast it to the pointer

        // Interpreting raw integer value as a pointer
        Function* newInteger = m_TypeModule->getFunction("newInteger()");
        Value*  smalltalkInt = jit.builder->CreateCall(newInteger, intResult, "intAsPtr.");
        intResultObject = jit.builder->CreateIntToPtr(smalltalkInt, ot.object->getPointerTo());
        intResultObject->setName("sum.");
    } else {
        // Returning a bool object depending on the compare operation result
        intResultObject = jit.builder->CreateSelect(intResult, m_globals.trueObject, m_globals.falseObject);
        intResultObject->setName("bool.");
    }

    // Jumping out the integersBlock to the value aggregator
    jit.builder->CreateBr(resultBlock);

    // Now the sendBinary block
    jit.builder->SetInsertPoint(sendBinaryBlock);
    // We need to create an arguments array and fill it with argument objects
    // Then send the message just like ordinary one

    Function* objectGetFields = m_TypeModule->getFunction("TObject::getFields()");

    Value* argumentsObject = createArray(jit, 2);
    Value* argFields       = jit.builder->CreateCall(objectGetFields, argumentsObject);

    Value* element0Ptr = jit.builder->CreateGEP(argFields, jit.builder->getInt32(0));
    jit.builder->CreateStore(leftValue, element0Ptr);

    Value* element1Ptr = jit.builder->CreateGEP(argFields, jit.builder->getInt32(1));
    jit.builder->CreateStore(rightValue, element1Ptr);

    Value* argumentsArray    = jit.builder->CreateBitCast(argumentsObject, ot.objectArray->getPointerTo());
    Value* sendMessageArgs[] = {
        jit.context, // calling context
        m_globals.binarySelectors[jit.instruction.low],
        argumentsArray
    };

    // Now performing a message call
    Value* sendMessageResult = 0;
    if (jit.methodHasBlockReturn) {
        sendMessageResult = jit.builder->CreateInvoke(m_runtimeAPI.sendMessage, resultBlock, jit.exceptionLandingPad, sendMessageArgs);
    } else {
        sendMessageResult = jit.builder->CreateCall(m_runtimeAPI.sendMessage, sendMessageArgs);

        // Jumping out the sendBinaryBlock to the value aggregator
        jit.builder->CreateBr(resultBlock);
    }
    sendMessageResult->setName("reply.");

    // Now the value aggregator block
    jit.builder->SetInsertPoint(resultBlock);

    // We do not know now which way the program will be executed,
    // so we need to aggregate two possible results one of which
    // will be then selected as a return value
    PHINode* phi = jit.builder->CreatePHI(ot.object->getPointerTo(), 2);
    phi->addIncoming(intResultObject, integersBlock);
    phi->addIncoming(sendMessageResult, sendBinaryBlock);

    // Result of sendBinary will be the value of phi function
    jit.pushValue(phi);
}

void MethodCompiler::doSendMessage(TJITContext& jit)
{
    Value* arguments = jit.popValue();

    // First of all we need to get the actual message selector
    //Function* getFieldFunction = m_TypeModule->getFunction("TObjectArray::getField(int)");

    //Value* literalArray    = jit.builder->CreateBitCast(jit.literals, ot.objectArray->getPointerTo());
    //Value* getFieldArgs[]  = { literalArray, jit.builder->getInt32(jit.instruction.low) };
    //Value* messageSelector = jit.builder->CreateCall(getFieldFunction, getFieldArgs);
    Value* messageSelectorPtr    = jit.builder->CreateGEP(jit.literals, jit.builder->getInt32(jit.instruction.low));
    Value* messageSelectorObject = jit.builder->CreateLoad(messageSelectorPtr);
    Value* messageSelector       = jit.builder->CreateBitCast(messageSelectorObject, ot.symbol->getPointerTo());

    //messageSelector = jit.builder->CreateBitCast(messageSelector, ot.symbol->getPointerTo());

    std::ostringstream ss;
    ss << "#" << jit.method->literals->getField(jit.instruction.low)->toString() << ".";
    messageSelector->setName(ss.str());

    // Forming a message parameters
    Value* sendMessageArgs[] = {
        jit.context,     // calling context
        messageSelector, // selector
        arguments        // message arguments
    };

    Value* result = 0;
    if (jit.methodHasBlockReturn) {
        // Creating basic block that will be branched to on normal invoke
        BasicBlock* nextBlock = BasicBlock::Create(m_JITModule->getContext(), "next.", jit.function);

        // Performing a function invoke
        result = jit.builder->CreateInvoke(m_runtimeAPI.sendMessage, nextBlock, jit.exceptionLandingPad, sendMessageArgs);

        // Switching builder to new block
        jit.builder->SetInsertPoint(nextBlock);
    } else {
        // Just calling the function. No block switching is required
        result = jit.builder->CreateCall(m_runtimeAPI.sendMessage, sendMessageArgs);
    }

    jit.pushValue(result);
}

void MethodCompiler::doSpecial(TJITContext& jit)
{
    TByteObject& byteCodes = * jit.method->byteCodes;
    uint8_t opcode = jit.instruction.low;

    BasicBlock::iterator iPreviousInst = jit.builder->GetInsertPoint();
    if (iPreviousInst != jit.builder->GetInsertBlock()->begin())
        --iPreviousInst;

    switch (opcode) {
        case SmalltalkVM::selfReturn:
            if (! iPreviousInst->isTerminator())
                jit.builder->CreateRet(jit.self);
            break;

        case SmalltalkVM::stackReturn:
            if ( !iPreviousInst->isTerminator() && jit.hasValue() )
                jit.builder->CreateRet(jit.popValue());
            break;

        case SmalltalkVM::blockReturn:
            if ( !iPreviousInst->isTerminator() && jit.hasValue()) {
                // Peeking the return value from the stack
                Value* value = jit.popValue();

                // Loading the target context information
                Value* creatingContextPtr = jit.builder->CreateStructGEP(jit.blockContext, 2);
                Value* targetContext      = jit.builder->CreateLoad(creatingContextPtr);

                // Emitting the TBlockReturn exception
                jit.builder->CreateCall2(m_runtimeAPI.emitBlockReturn, value, targetContext);

                // This will never be called
                jit.builder->CreateUnreachable();
            }
            break;

        case SmalltalkVM::duplicate:
            jit.pushValue(jit.lastValue());
            break;

        case SmalltalkVM::popTop:
            if (jit.hasValue())
                jit.popValue();
            break;

        case SmalltalkVM::branch: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer += 2; // skipping the branch offset data

            if (!iPreviousInst->isTerminator()) {
                // Finding appropriate branch target
                // from the previously stored basic blocks
                BasicBlock* target = m_targetToBlockMap[targetOffset];
                jit.builder->CreateBr(target);
            }
        } break;

        case SmalltalkVM::branchIfTrue:
        case SmalltalkVM::branchIfFalse: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer += 2; // skipping the branch offset data

            if (!iPreviousInst->isTerminator()) {
                // Finding appropriate branch target
                // from the previously stored basic blocks
                BasicBlock* targetBlock = m_targetToBlockMap[targetOffset];

                // This is a block that goes right after the branch instruction.
                // If branch condition is not met execution continues right after
                BasicBlock* skipBlock = BasicBlock::Create(m_JITModule->getContext(), "branchSkip.", jit.function);

                // Creating condition check
                Value* boolObject = (opcode == SmalltalkVM::branchIfTrue) ? m_globals.trueObject : m_globals.falseObject;
                Value* condition  = jit.popValue();
                Value* boolValue  = jit.builder->CreateICmpEQ(condition, boolObject);
                jit.builder->CreateCondBr(boolValue, targetBlock, skipBlock);

                // Switching to a newly created block
                jit.builder->SetInsertPoint(skipBlock);
            }
        } break;

        case SmalltalkVM::breakpoint:
            // TODO
            break;

        default:
            printf("JIT: unknown special opcode %d\n", opcode);
    }
}

void MethodCompiler::doPrimitive(TJITContext& jit)
{
    uint32_t opcode = jit.method->byteCodes->getByte(jit.bytePointer++);
    outs() << "Primitive opcode = " << opcode << "\n";

    Value* primitiveResult = 0;
    BasicBlock* primitiveFailed = BasicBlock::Create(m_JITModule->getContext(), "primitiveFailed", jit.function);
    bool primitiveShouldNeverFail = false;

    switch (opcode) {
        case SmalltalkVM::objectsAreEqual: {
            Value* object2 = jit.popValue();
            Value* object1 = jit.popValue();

            Value* result    = jit.builder->CreateICmpEQ(object1, object2);
            Value* boolValue = jit.builder->CreateSelect(result, m_globals.trueObject, m_globals.falseObject);

            primitiveResult = boolValue;
            primitiveShouldNeverFail = true;
        } break;

        case SmalltalkVM::getClass: {
            Value*    object   = jit.popValue();
            Function* getClass = m_TypeModule->getFunction("TObject::getClass()");
            Value*    klass    = jit.builder->CreateCall(getClass, object, "class");
            primitiveResult = klass;
            primitiveShouldNeverFail = true;
        } break;

        // TODO ioGetchar ioPutChar

        case SmalltalkVM::getSize: {
            Function* isSmallInt = m_TypeModule->getFunction("isSmallInteger()");
            Function* getSize    = m_TypeModule->getFunction("TObject::getSize()");
            Function* newInteger = m_TypeModule->getFunction("newInteger()");
            
            Value* object           = jit.popValue();
            Value* objectIsSmallInt = jit.builder->CreateCall(isSmallInt, object, "isSmallInt");
            
            BasicBlock* whenSmallInt = BasicBlock::Create(m_JITModule->getContext(), "whenSmallInt", jit.function);
            BasicBlock* whenObject   = BasicBlock::Create(m_JITModule->getContext(), "whenObject", jit.function);
            jit.builder->CreateCondBr(objectIsSmallInt, whenSmallInt, whenObject);
            
            jit.builder->SetInsertPoint(whenSmallInt);
            Value* result = jit.builder->CreateCall(newInteger, jit.builder->getInt32(0));
            jit.builder->CreateRet(result);
            
            jit.builder->SetInsertPoint(whenObject);
            Value* size       = jit.builder->CreateCall(getSize, object, "size");
            Value* sizeObject = jit.builder->CreateCall(newInteger, size);
            primitiveResult = sizeObject;
            primitiveShouldNeverFail = true;
        } break;

        // TODO new process

        case SmalltalkVM::allocateObject: { // FIXME pointer safety
            Value* sizeObject  = jit.popValue();
            Value* klass       = jit.popValue();

            Function* getValue = m_TypeModule->getFunction("getIntegerValue()");
            Value*    size     = jit.builder->CreateCall(getValue, sizeObject, "size.");

            Function* getSlotSize = m_TypeModule->getFunction("getSlotSize()");
            Value*    slotSize    = jit.builder->CreateCall(getSlotSize, size, "slotSize.");

            Value*    args[]      = { klass, slotSize };
            Value*    newInstance = jit.builder->CreateCall(m_runtimeAPI.newOrdinaryObject, args, "instance.");

            primitiveResult = newInstance;
            primitiveShouldNeverFail = true;
        } break;

        case SmalltalkVM::allocateByteArray: { // FIXME pointer safety
            Value*    sizeObject  = jit.popValue();
            Value*    klass       = jit.popValue();

            Function* getValue    = m_TypeModule->getFunction("getIntegerValue()");
            Value*    dataSize    = jit.builder->CreateCall(getValue, sizeObject, "dataSize.");

            Value*    args[]      = { klass, dataSize };
            Value*    newInstance = jit.builder->CreateCall(m_runtimeAPI.newBinaryObject, args, "instance.");

            primitiveResult = newInstance;
            primitiveShouldNeverFail = true;
        } break;

        case SmalltalkVM::cloneByteObject: { // FIXME pointer safety
            Value*    klass    = jit.popValue();
            Value*    original = jit.popValue();

            Function* getSize  = m_TypeModule->getFunction("TObject::getSize()");
            Value*    dataSize = jit.builder->CreateCall(getSize, original, "dataSize.");

            Value*    args[]   = { klass, dataSize };
            Value*    clone    = jit.builder->CreateCall(m_runtimeAPI.newBinaryObject, args, "clone.");

            primitiveResult = clone;
            primitiveShouldNeverFail = true;
        } break;

        case SmalltalkVM::integerNew:
            primitiveResult = jit.popValue(); // TODO long integers
            primitiveShouldNeverFail = true;
            break;

        case SmalltalkVM::blockInvoke: {
            Value* object = jit.popValue();
            Value* block  = jit.builder->CreateBitCast(object, ot.block->getPointerTo());

            int32_t argCount = jit.instruction.low - 1;

            Value* blockAsContext = jit.builder->CreateBitCast(block, ot.context->getPointerTo());
            Value* blockTempsPtr  = jit.builder->CreateStructGEP(blockAsContext, 3);
            Value* blockTemps     = jit.builder->CreateLoad(blockTempsPtr);
            Value* blockTempsObject = jit.builder->CreateBitCast(blockTemps, ot.object->getPointerTo());

            Function* getFields = m_TypeModule->getFunction("TObject::getFields()");
            Value*    fields    = jit.builder->CreateCall(getFields, blockTempsObject);

            Function* getSize   = m_TypeModule->getFunction("TObject::getSize()");
            Value*    tempsSize = jit.builder->CreateCall(getSize, blockTempsObject);

            Value* argumentLocationPtr = jit.builder->CreateStructGEP(block, 1);
            Value* argumentLocation    = jit.builder->CreateLoad(argumentLocationPtr);

            BasicBlock* tempsChecked = BasicBlock::Create(m_JITModule->getContext(), "tempsChecked.", jit.function);

            //Checking the passed temps size TODO unroll stack
            Value* blockAcceptsArgCount = jit.builder->CreateSub(tempsSize, argumentLocation);
            Value* tempSizeOk = jit.builder->CreateICmpSLE(blockAcceptsArgCount, jit.builder->getInt32(argCount));
            jit.builder->CreateCondBr(tempSizeOk, tempsChecked, primitiveFailed);
            jit.builder->SetInsertPoint(tempsChecked);
            
            // Storing values in the block's wrapping context
            for (uint32_t index = argCount - 1, count = argCount; count > 0; index--, count--)
            {
                // (*blockTemps)[argumentLocation + index] = stack[--ec.stackTop];
                Value* fieldIndex = jit.builder->CreateAdd(argumentLocation, jit.builder->getInt32(index));
                Value* fieldPtr   = jit.builder->CreateGEP(fields, fieldIndex);
                Value* argument   = jit.popValue();
                jit.builder->CreateStore(argument, fieldPtr);
            }

            Value* args[] = { block, jit.context };
            Value* result = jit.builder->CreateCall(m_runtimeAPI.invokeBlock, args);

            primitiveResult = result;
        } break;

        case SmalltalkVM::arrayAt:
        case SmalltalkVM::arrayAtPut: {
            Value* indexObject = jit.popValue();
            Value* arrayObject = jit.popValue();
            Value* valueObejct = 0;

            Function* getValue = m_TypeModule->getFunction("getIntegerValue()");
            Value*    index    = jit.builder->CreateCall(getValue, indexObject, "index.");

            Function* getFields = m_TypeModule->getFunction("TObject::getFields()");
            Value*    fields    = jit.builder->CreateCall(getFields, arrayObject);
            Value*    fieldPtr  = jit.builder->CreateGEP(fields, index);

            // TODO Check boundaries and small ints

            if (opcode == SmalltalkVM::arrayAtPut) {
                valueObejct = jit.popValue();
                jit.builder->CreateStore(valueObejct, fieldPtr);
                primitiveResult = arrayObject; // valueObejct;
            } else {
                primitiveResult = jit.builder->CreateLoad(fieldPtr);
            }
        } break;

        case SmalltalkVM::stringAt:
        case SmalltalkVM::stringAtPut: {
            Value* indexObject  = jit.popValue();
            Value* stringObject = jit.popValue();
            Value* valueObejct  = 0;

            BasicBlock* indexChecked = BasicBlock::Create(m_JITModule->getContext(), "indexChecked.", jit.function);

            //Checking whether index is Smallint
            Function* isSmallInt      = m_TypeModule->getFunction("isSmallInteger()");
            Value*    indexIsSmallInt = jit.builder->CreateCall(isSmallInt, indexObject, "indexIsSmallInt.");

            // Acquiring integer value of the index (from the smalltalk's TInteger)
            Function* getValue = m_TypeModule->getFunction("getIntegerValue()");
            Value*    index    = jit.builder->CreateCall(getValue, indexObject, "index.");
            Value* actualIndex = jit.builder->CreateSub(index, jit.builder->getInt32(1), "actualIndex.");
            
            //Checking boundaries TODO > 0
            Function* getSize    = m_TypeModule->getFunction("TObject::getSize()");
            Value*    stringSize = jit.builder->CreateCall(getSize, stringObject, "stringSize.");
            Value*    boundaryOk = jit.builder->CreateICmpSLT(actualIndex, stringSize, "boundaryOk.");

            Value* indexOk = jit.builder->CreateAnd(indexIsSmallInt, boundaryOk, "indexOk.");
            jit.builder->CreateCondBr(indexOk, indexChecked, primitiveFailed);
            jit.builder->SetInsertPoint(indexChecked);

            // Getting access to the actual indexed byte location
            Function* getFields = m_TypeModule->getFunction("TObject::getFields()");
            Value*    fields    = jit.builder->CreateCall(getFields, stringObject);
            Value*    bytes     = jit.builder->CreateBitCast(fields, Type::getInt8Ty(m_JITModule->getContext())->getPointerTo());
            Value*    bytePtr   = jit.builder->CreateGEP(bytes, actualIndex);

            if (opcode == SmalltalkVM::stringAtPut) {
                // Popping new value from the stack, getting actual integral value from the TInteger
                // then shrinking it to the 1 byte representation and inserting into the pointed location
                
                valueObejct = jit.popValue(); 
                Value* valueInt = jit.builder->CreateCall(getValue, valueObejct);
                Value* byte = jit.builder->CreateTrunc(valueInt, Type::getInt8Ty(m_JITModule->getContext()));
                jit.builder->CreateStore(byte, bytePtr); 

                primitiveResult = stringObject;
            } else {
                // Loading string byte pointed by the pointer,
                // expanding it to the 4 byte integer and returning
                // as TInteger value
                
                Value* byte = jit.builder->CreateLoad(bytePtr);
                Value* expandedByte = jit.builder->CreateZExt(byte, Type::getInt32Ty(m_JITModule->getContext()));
                Function* newInt = m_TypeModule->getFunction("newInteger()");
                primitiveResult = jit.builder->CreateCall(newInt, expandedByte);
            }
        } break;


        case SmalltalkVM::smallIntAdd:        // 10
        case SmalltalkVM::smallIntDiv:        // 11
        case SmalltalkVM::smallIntMod:        // 12
        case SmalltalkVM::smallIntLess:       // 13
        case SmalltalkVM::smallIntEqual:      // 14
        case SmalltalkVM::smallIntMul:        // 15
        case SmalltalkVM::smallIntSub:        // 16
        case SmalltalkVM::smallIntBitOr:      // 36
        case SmalltalkVM::smallIntBitAnd:     // 37
        case SmalltalkVM::smallIntBitShift: { // 39
            Value* rightObject = jit.popValue();
            Value* leftObject  = jit.popValue();

            Function* isSmallInt  = m_TypeModule->getFunction("isSmallInteger()");
            Function* newInteger  = m_TypeModule->getFunction("newInteger()");
            Function* getIntValue = m_TypeModule->getFunction("getIntegerValue()");

            Value*    rightIsInt  = jit.builder->CreateCall(isSmallInt, rightObject);
            Value*    leftIsInt   = jit.builder->CreateCall(isSmallInt, leftObject);
            Value*    isSmallInts = jit.builder->CreateAnd(rightIsInt, leftIsInt);

            BasicBlock* areInts  = BasicBlock::Create(m_JITModule->getContext(), "areInts.", jit.function);
            jit.builder->CreateCondBr(isSmallInts, areInts, primitiveFailed);

            jit.builder->SetInsertPoint(areInts);
            Value* rightOperand = jit.builder->CreateCall(getIntValue, rightObject);
            Value* leftOperand  = jit.builder->CreateCall(getIntValue, leftObject);

            switch(opcode) { //FIXME move to function
                case SmalltalkVM::smallIntAdd: {
                    Value* intResult = jit.builder->CreateAdd(leftOperand, rightOperand);
                    //FIXME overflow
                    primitiveResult  = jit.builder->CreateCall(newInteger, intResult);
                } break;
                case SmalltalkVM::smallIntDiv: {
                    Value*      isZero = jit.builder->CreateICmpEQ(rightOperand, jit.builder->getInt32(0));
                    BasicBlock* divBB  = BasicBlock::Create(m_JITModule->getContext(), "div.", jit.function);
                    jit.builder->CreateCondBr(isZero, divBB, primitiveFailed);
                    
                    jit.builder->SetInsertPoint(divBB);
                    Value* intResult = jit.builder->CreateExactSDiv(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(newInteger, intResult);
                } break;
                case SmalltalkVM::smallIntMod: {
                    Value*      isZero = jit.builder->CreateICmpEQ(rightOperand, jit.builder->getInt32(0));
                    BasicBlock* modBB  = BasicBlock::Create(m_JITModule->getContext(), "mod.", jit.function);
                    jit.builder->CreateCondBr(isZero, modBB, primitiveFailed);
                    
                    jit.builder->SetInsertPoint(modBB);
                    Value* intResult = jit.builder->CreateSRem(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(newInteger, intResult);
                } break;
                case SmalltalkVM::smallIntLess: {
                    Value* condition = jit.builder->CreateICmpSLT(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
                } break;
                case SmalltalkVM::smallIntEqual: {
                    Value* condition = jit.builder->CreateICmpEQ(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
                } break;
                case SmalltalkVM::smallIntMul: {
                    Value* intResult = jit.builder->CreateMul(leftOperand, rightOperand);
                    //FIXME overflow
                    primitiveResult  = jit.builder->CreateCall(newInteger, intResult);
                } break;
                case SmalltalkVM::smallIntSub: {
                    Value* intResult = jit.builder->CreateSub(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(newInteger, intResult);
                } break;
                case SmalltalkVM::smallIntBitOr: {
                    Value* intResult = jit.builder->CreateOr(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(newInteger, intResult);
                } break;
                case SmalltalkVM::smallIntBitAnd: {
                    Value* intResult = jit.builder->CreateAnd(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(newInteger, intResult);
                } break;
                //case SmalltalkVM::smallIntBitShift: { //TODO
                //} break;
                default: {
                    Value* arguments[] = { jit.builder->getInt8(opcode), leftOperand, rightOperand };
                    primitiveResult    = jit.builder->CreateCall(m_runtimeAPI.performSmallInt, arguments, "result.");
                }
            }
        } break;

        case SmalltalkVM::bulkReplace: {
            Value* destination            = jit.popValue();
            Value* sourceStartOffset      = jit.popValue();
            Value* source                 = jit.popValue();
            Value* destinationStopOffset  = jit.popValue();
            Value* destinationStartOffset = jit.popValue();

            Value* arguments[]  = { destination, sourceStartOffset, source, destinationStopOffset, destinationStartOffset };
            Value* isSucceeded  = jit.builder->CreateCall(m_runtimeAPI.bulkReplace, arguments, "ok.");
            //FIXME remove CreateSelect
            Value* resultObject = jit.builder->CreateSelect(isSucceeded, destination, m_globals.nilObject);

            primitiveResult = resultObject;
        } break;

        default:
            outs() << "JIT: Unknown primitive code " << opcode << "\n";
    }

    if (primitiveShouldNeverFail) {
        BasicBlock* primitiveSucceeded = BasicBlock::Create(m_JITModule->getContext(), "primitiveSucceeded.", jit.function);
        
        jit.builder->CreateCondBr(jit.builder->getTrue(), primitiveSucceeded, primitiveFailed);
        jit.builder->SetInsertPoint(primitiveSucceeded);
    }

    jit.builder->CreateRet(primitiveResult);
    jit.builder->SetInsertPoint(primitiveFailed);

    jit.pushValue(m_globals.nilObject);
}