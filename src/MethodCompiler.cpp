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
#include <llvm/Support/CFG.h>
#include <iostream>
#include <sstream>

using namespace llvm;

Value* TDeferredValue::get()
{
    IRBuilder<>& builder = * m_jit->builder;
    Module* jitModule = JITRuntime::Instance()->getModule();
    Function* getObjectField = jitModule->getFunction("getObjectField");
    Type* objectPtrType = jitModule->getTypeByName("TObject")->getPointerTo();
    
    switch (m_operation) {
        case loadHolder:
            return builder.CreateLoad(m_argument);
        
        case loadArgument: {
            Value* context   = m_jit->getCurrentContext();
            Value* pargs     = builder.CreateStructGEP(context, 2); // args
            Value* arguments = builder.CreateLoad(pargs);
            Value* pobject   = builder.CreateBitCast(arguments, objectPtrType);
            Value* argument  = builder.CreateCall2(getObjectField, pobject, builder.getInt32(m_index));
            
            std::ostringstream ss;
            ss << "arg" << (uint32_t) m_index << ".";
            argument->setName(ss.str());
            
            return argument;
        } break;
        
        case loadInstance: {
            Value* self    = m_jit->getSelf();
            Value* field = builder.CreateCall2(getObjectField, self, builder.getInt32(m_index));
            
            std::ostringstream ss;
            ss << "field" << (uint32_t) m_index << ".";
            field->setName(ss.str());
            
            return field;
        } break;
        
        case loadTemporary: {
            Value* context   = m_jit->getCurrentContext();
            Value* ptemps    = builder.CreateStructGEP(context, 3); // temporaries
            Value* temps     = builder.CreateLoad(ptemps);
            Value* pobject   = builder.CreateBitCast(temps, objectPtrType);
            Value* temporary = builder.CreateCall2(getObjectField, pobject, builder.getInt32(m_index));
            
            std::ostringstream ss;
            ss << "temp" << (uint32_t) m_index << ".";
            temporary->setName(ss.str());
            
            return temporary;
        } break;
        
        case loadLiteral: {
            return m_jit->getLiteral(m_index);
        } break;
        
        default:
            outs() << "Unknown deferred operation: " << m_operation << "\n";
            return 0;
    }
}

Value* MethodCompiler::TJITContext::getLiteral(uint32_t index)
{
    Module* jitModule = JITRuntime::Instance()->getModule();
    Function* getObjectField = jitModule->getFunction("getObjectField");
    Type* objectPtrType = jitModule->getTypeByName("TObject")->getPointerTo();
    
    Value* context   = getCurrentContext();
    Value* pmethod   = builder->CreateStructGEP(context, 1); // method*
    Value* method    = builder->CreateLoad(pmethod);
    
    Value* pliterals = builder->CreateStructGEP(method, 3); // literals*
    Value* literals  = builder->CreateLoad(pliterals);
    
    Value* pobject   = builder->CreateBitCast(literals, objectPtrType);
    Value* literal   = builder->CreateCall2(getObjectField, pobject, builder->getInt32(index));
    
    std::ostringstream ss;
    ss << "lit" << (uint32_t) index << ".";
    literal->setName(ss.str());
    
    return literal;
}

Value* MethodCompiler::TJITContext::getMethodClass()
{
    Value* context   = getCurrentContext();
    Value* pmethod   = builder->CreateStructGEP(context, 1); // method*
    Value* method    = builder->CreateLoad(pmethod);
    Value* pklass    = builder->CreateStructGEP(method, 6); // class*
    Value* klass     = builder->CreateLoad(pklass);
    
    klass->setName("class.");
    return klass;
}

void MethodCompiler::TJITContext::pushValue(TStackValue* value)
{
    // Values are always pushed to the local stack
    basicBlockContexts[builder->GetInsertBlock()].valueStack.push_back(value);
}

void MethodCompiler::TJITContext::pushValue(Value* value)
{
    // Values are always pushed to the local stack
    basicBlockContexts[builder->GetInsertBlock()].valueStack.push_back(new TPlainValue(value));
}

Value* MethodCompiler::TJITContext::lastValue()
{
    TValueStack& valueStack = basicBlockContexts[builder->GetInsertBlock()].valueStack;
//     if (! valueStack.empty())
//         return valueStack.back()->get();
    
    // Popping value from the referer's block
    // and creating phi function if necessary
    Value* value = popValue();
    
    // Pushing the value locally (may be phi)
    valueStack.push_back(new TPlainValue(value));
    
    // Returning it as a last value
    return value;
}

bool MethodCompiler::TJITContext::hasValue()
{
    TBasicBlockContext& blockContext = basicBlockContexts[builder->GetInsertBlock()];
    
    // If local stack is not empty, then we definitly have some value
    if (! blockContext.valueStack.empty())
        return true;
    
    // If not, checking the possible referers
    if (blockContext.referers.size() == 0)
        return false; // no referers == no value
    
    // FIXME This is not correct in a case of dummy transitive block with an only simple branch
    //       Every referer should have equal number of values on the stack
    //       so we may check any referer's stack to see if it has value
    return ! basicBlockContexts[*blockContext.referers.begin()].valueStack.empty();
}

Value* MethodCompiler::TJITContext::popValue(BasicBlock* overrideBlock /* = 0*/)
{
    TBasicBlockContext& blockContext = basicBlockContexts[overrideBlock ? overrideBlock : builder->GetInsertBlock()];
    TValueStack& valueStack = blockContext.valueStack;
    
    if (! valueStack.empty()) {
        // If local stack is not empty
        // then we simply pop the value from it
        
        BasicBlock* currentBasicBlock = builder->GetInsertBlock();
        BasicBlock::iterator currentInsertPoint = builder->GetInsertPoint();
        if (overrideBlock) {
            Instruction* terminator = overrideBlock->getTerminator();
            if (terminator)
                builder->SetInsertPoint(terminator);
            else 
                builder->SetInsertPoint(overrideBlock);
        }
        
        TStackValue* stackValue = valueStack.back();
        Value* value = stackValue->get(); // NOTE May perform code injection
        delete stackValue;
        valueStack.pop_back();
        
        if (overrideBlock)
            builder->SetInsertPoint(currentBasicBlock, currentInsertPoint);
        
        return value;
    } else {
        // If value stack is empty then it means that we're dealing with
        // a value pushed in the predcessor block (or a stack underflow)
        
        // If there is a single predcessor, then we simply pop that value
        // If there are several predcessors we need to create a phi function
        switch (blockContext.referers.size()) {
            case 0: 
                /* TODO no referers, empty local stack and pop operation = error */ 
                outs() << "Value stack underflow\n";
                exit(1);
                return compiler->m_globals.nilObject;
                
            case 1: {
                // Recursively processing referer's block
                BasicBlock* referer = *blockContext.referers.begin();
                Value* value = popValue(referer);
                return value;
            } break;
            
            default: {
                // Storing current insert position for further use
                BasicBlock* currentBasicBlock = builder->GetInsertBlock();
                BasicBlock::iterator currentInsertPoint = builder->GetInsertPoint();
                
                BasicBlock* insertBlock = overrideBlock ? overrideBlock : currentBasicBlock;  
                BasicBlock::iterator firstInsertionPoint = insertBlock->getFirstInsertionPt();
                
                if (overrideBlock) {
                    builder->SetInsertPoint(overrideBlock, firstInsertionPoint);
                } else {
                    if (firstInsertionPoint != insertBlock->end())
                        builder->SetInsertPoint(currentBasicBlock, firstInsertionPoint);
                }
                
                // Creating a phi function at the beginning of the block
                const uint32_t numReferers = blockContext.referers.size();
                PHINode* phi = builder->CreatePHI(compiler->m_baseTypes.object->getPointerTo(), numReferers, "phi.");
                    
                // Filling incoming nodes with values from the referer stacks
                TRefererSet::iterator iReferer = blockContext.referers.begin();
                for (; iReferer != blockContext.referers.end(); ++iReferer) {
                    // FIXME non filled block will not yet have the value
                    //       we need to store them to a special post processing list
                    //       and update the current phi function when value will be available
                    
                    Value* value = popValue(*iReferer);
                    phi->addIncoming(value, *iReferer);
                }
                
                if (overrideBlock || firstInsertionPoint != insertBlock->end())
                    builder->SetInsertPoint(currentBasicBlock, currentInsertPoint);
                
                return phi;
            }
        }
    }
}

Function* MethodCompiler::createFunction(TMethod* method)
{
    Type* methodParams[] = { m_baseTypes.context->getPointerTo() };
    FunctionType* functionType = FunctionType::get(
        m_baseTypes.object->getPointerTo(), // function return value
        methodParams,              // parameters
        false                      // we're not dealing with vararg
    );
    
    std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
    Function* function = cast<Function>( m_JITModule->getOrInsertFunction(functionName, functionType));
    function->setCallingConv(CallingConv::C); //Anyway C-calling conversion is default
    function->setGC("shadow-stack");
    return function;
}

Value* MethodCompiler::allocateRoot(TJITContext& jit, Type* type)
{
    // Storing current edit location
    BasicBlock* insertBlock = jit.builder->GetInsertBlock();
    BasicBlock::iterator insertPoint = jit.builder->GetInsertPoint();
    
    // Switching to the preamble
    jit.builder->SetInsertPoint(jit.preamble, jit.preamble->begin());
    
    // Allocating the object holder
    Value* holder = jit.builder->CreateAlloca(type, 0, "holder.");
    
    // Registering holder as a GC root
    Value* stackRoot = jit.builder->CreateBitCast(holder, jit.builder->getInt8PtrTy()->getPointerTo(), "root.");
    Function* gcrootIntrinsic = getDeclaration(m_JITModule, Intrinsic::gcroot);
    jit.builder->CreateCall2(gcrootIntrinsic, stackRoot, ConstantPointerNull::get(jit.builder->getInt8PtrTy()));
    
    // Returning to the original edit location
    jit.builder->SetInsertPoint(insertBlock, insertPoint);
    
    return holder;
}

Value* MethodCompiler::protectPointer(TJITContext& jit, Value* value)
{
    // Allocating holder
    Value* holder = allocateRoot(jit, value->getType());
    
    // Storing value to the holder to protect the pointer
    jit.builder->CreateStore(value, holder);
    
    return holder;           
}

void MethodCompiler::writePreamble(TJITContext& jit, bool isBlock)
{
    Value* context = 0;
    
    if (! isBlock) {
        // This is a regular function
        context = (Value*) (jit.function->arg_begin());
        context->setName("context");
    } else {
        // This is a block function
        Value* blockContext = (Value*) (jit.function->arg_begin());
        blockContext->setName("blockContext");
        
        context = jit.builder->CreateBitCast(blockContext, m_baseTypes.context->getPointerTo());
    }
    context->setName("contextParameter");
    
    // Protecting the context holder
    jit.contextHolder = protectPointer(jit, context);
    jit.contextHolder->setName("pContext");
    
    // Storing self pointer
    Value* pargs     = jit.builder->CreateStructGEP(context, 2);
    Value* arguments = jit.builder->CreateLoad(pargs);
    Value* pobject   = jit.builder->CreateBitCast(arguments, m_baseTypes.object->getPointerTo());
    Value* self      = jit.builder->CreateCall2(m_baseFunctions.TObject__getField, pobject, jit.builder->getInt32(0));
    jit.selfHolder   = protectPointer(jit, self);
    jit.selfHolder->setName("pSelf");
}

Value* MethodCompiler::TJITContext::getCurrentContext()
{
    return builder->CreateLoad(contextHolder, "context.");
}
    
Value* MethodCompiler::TJITContext::getSelf()
{
    return builder->CreateLoad(selfHolder, "self.");
}

bool MethodCompiler::scanForBlockReturn(TJITContext& jit, uint32_t byteCount/* = 0*/)
{
    uint32_t previousBytePointer = jit.bytePointer;
    
    TByteObject& byteCodes   = * jit.method->byteCodes;
    uint32_t     stopPointer = jit.bytePointer + (byteCount ? byteCount : byteCodes.getSize());
    
    // Processing the method's bytecodes
    while (jit.bytePointer < stopPointer) {
//         uint32_t currentOffset = jit.bytePointer;
//         printf("scanForBlockReturn: Processing offset %d / %d \n", currentOffset, stopPointer);
        
        // Decoding the pending instruction (TODO move to a function)
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == opcode::extended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[jit.bytePointer++];
        }
        
        if (instruction.high == opcode::pushBlock) {
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
        
        if (instruction.high == opcode::doPrimitive) {
            jit.bytePointer++; // skipping primitive number
            continue;
        }
        
        // We're now looking only for branch bytecodes
        if (instruction.high != opcode::doSpecial)
            continue;
        
        switch (instruction.low) {
            case special::blockReturn:
                // outs() << "Found a block return at offset " << currentOffset << "\n";
                
                // Resetting bytePointer to an old value
                jit.bytePointer = previousBytePointer;
                return true;
            
            case special::branch:
            case special::branchIfFalse:
            case special::branchIfTrue:
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
//         uint32_t currentOffset = jit.bytePointer;
        // printf("scanForBranches: Processing offset %d / %d \n", currentOffset, stopPointer);
        
        // Decoding the pending instruction (TODO move to a function)
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == opcode::extended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[jit.bytePointer++];
        }
        
        if (instruction.high == opcode::pushBlock) {
            // Skipping the nested block's bytecodes
            uint16_t newBytePointer = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer = newBytePointer;
            continue;
        }
        
        if (instruction.high == opcode::doPrimitive) {
            jit.bytePointer++; // skipping primitive number
            continue;
        }
        
        // We're now looking only for branch bytecodes
        if (instruction.high != opcode::doSpecial)
            continue;
        
        switch (instruction.low) {
            case special::branch:
            case special::branchIfTrue:
            case special::branchIfFalse: {
                // Loading branch target bytecode offset
                uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
                jit.bytePointer += 2; // skipping the branch offset data
                
                if (m_targetToBlockMap.find(targetOffset) == m_targetToBlockMap.end()) {
                    // Creating the referred basic block and inserting it into the function
                    // Later it will be filled with instructions and linked to other blocks
                    BasicBlock* targetBasicBlock = BasicBlock::Create(m_JITModule->getContext(), "branch.", jit.function);
                    m_targetToBlockMap[targetOffset] = targetBasicBlock;

                }
                
                //outs() << "Branch site: " << currentOffset << " -> " << targetOffset << " (" << m_targetToBlockMap[targetOffset]->getName() << ")\n";
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
    return arrayObject; //protectPointer(jit, arrayObject);
}

Function* MethodCompiler::compileMethod(TMethod* method, TContext* callingContext)
{
    TJITContext  jit(this, method, callingContext);
    
    // Creating the function named as "Class>>method"
    jit.function = createFunction(method);
    
    // Creating the preamble basic block and inserting it into the function
    // It will contain basic initialization code (args, temps and so on)
    jit.preamble = BasicBlock::Create(m_JITModule->getContext(), "preamble", jit.function);
    
    // Creating the instruction builder
    jit.builder = new IRBuilder<>(jit.preamble);

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
    jit.builder->SetInsertPoint(jit.preamble);
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
        
        std::map<uint32_t, BasicBlock*>::iterator iBlock = m_targetToBlockMap.find(currentOffset);
        if (iBlock != m_targetToBlockMap.end()) {
            // Somewhere in the code we have a branch instruction that
            // points to the current offset. We need to end the current
            // basic block and start a new one, linking previous
            // basic block to a new one.
            
            BasicBlock* newBlock = iBlock->second; // Picking a basic block
            BasicBlock::iterator iInst = jit.builder->GetInsertPoint();
            
            if (iInst != jit.builder->GetInsertBlock()->begin())
                --iInst;
            
//             outs() << "Prev is: " << *newBlock << "\n";
            if (! iInst->isTerminator()) {
                jit.builder->CreateBr(newBlock); // Linking current block to a new one
                // Updating the block referers
                
                // Inserting current block as a referer to the newly created one
                // Popping the value may result in popping the referer's stack
                // or even generation of phi function if there are several referers  
                jit.basicBlockContexts[newBlock].referers.insert(jit.builder->GetInsertBlock());
            }
            
            jit.builder->SetInsertPoint(newBlock); // and switching builder to a new block
        }
        
        // First of all decoding the pending instruction
        jit.instruction.low = (jit.instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        jit.instruction.high >>= 4;
        if (jit.instruction.high == opcode::extended) {
            jit.instruction.high =  jit.instruction.low;
            jit.instruction.low  =  byteCodes[jit.bytePointer++];
        }
        
//         printOpcode(jit.instruction);
        
//        uint32_t instCountBefore = jit.builder->GetInsertBlock()->getInstList().size();

        // Then writing the code
        switch (jit.instruction.high) {
            // TODO Boundary checks against container's real size
            case opcode::pushInstance:      doPushInstance(jit);    break;
            case opcode::pushArgument:      doPushArgument(jit);    break;
            case opcode::pushTemporary:     doPushTemporary(jit);   break;
            case opcode::pushLiteral:       doPushLiteral(jit);     break;
            case opcode::pushConstant:      doPushConstant(jit);    break;
            
            case opcode::pushBlock:         doPushBlock(currentOffset, jit); break;
            
            case opcode::assignTemporary:   doAssignTemporary(jit); break;
            case opcode::assignInstance:    doAssignInstance(jit);  break;
            
            case opcode::markArguments:     doMarkArguments(jit);   break;
            case opcode::sendUnary:         doSendUnary(jit);       break;
            case opcode::sendBinary:        doSendBinary(jit);      break;
            case opcode::sendMessage:       doSendMessage(jit);     break;
            
            case opcode::doSpecial:         doSpecial(jit);         break;
            case opcode::doPrimitive:       doPrimitive(jit);       break;
            
            default:
                fprintf(stderr, "JIT: Invalid opcode %d at offset %d in method %s\n",
                        jit.instruction.high, jit.bytePointer, jit.method->name->toString().c_str());
        }

//         uint32_t instCountAfter = jit.builder->GetInsertBlock()->getInstList().size();
// 
//          if (instCountAfter > instCountBefore)
//             outs() << * --jit.builder->GetInsertPoint() << "\n";         
//                outs() << "[" << currentOffset << "] " << (jit.function->getName()) << ":" << (jit.builder->GetInsertBlock()->getName()) << ": " << *(--jit.builder->GetInsertPoint()) << "\n";
    }
}

void MethodCompiler::writeLandingPad(TJITContext& jit)
{
    // outs() << "Writing landing pad\n";
    
    jit.exceptionLandingPad = BasicBlock::Create(m_JITModule->getContext(), "landingPad", jit.function);
    jit.builder->SetInsertPoint(jit.exceptionLandingPad);
    
    Type* caughtType = StructType::get(jit.builder->getInt8PtrTy(), jit.builder->getInt32Ty(), NULL);
    
    LandingPadInst* exceptionStruct = jit.builder->CreateLandingPad(caughtType, m_exceptionAPI.gcc_personality, 1);
    exceptionStruct->addClause(m_exceptionAPI.blockReturnType);
    
    Value* exceptionObject  = jit.builder->CreateExtractValue(exceptionStruct, 0);
    Value* thrownException  = jit.builder->CreateCall(m_exceptionAPI.cxa_begin_catch, exceptionObject);
    Value* blockReturn      = jit.builder->CreateBitCast(thrownException, m_baseTypes.blockReturn->getPointerTo());
    
    Value* returnValue      = jit.builder->CreateLoad( jit.builder->CreateStructGEP(blockReturn, 0) );
    Value* targetContext    = jit.builder->CreateLoad( jit.builder->CreateStructGEP(blockReturn, 1) );
    
    jit.builder->CreateCall(m_exceptionAPI.cxa_end_catch);
    
    Value* compareTargets = jit.builder->CreateICmpEQ(jit.getCurrentContext(), targetContext);
    BasicBlock* returnBlock  = BasicBlock::Create(m_JITModule->getContext(), "return",  jit.function);
    BasicBlock* rethrowBlock = BasicBlock::Create(m_JITModule->getContext(), "rethrow", jit.function);
    
    jit.builder->CreateCondBr(compareTargets, returnBlock, rethrowBlock);
    
    jit.builder->SetInsertPoint(returnBlock);
    jit.builder->CreateRet(returnValue);
    
    jit.builder->SetInsertPoint(rethrowBlock);
    jit.builder->CreateResume(exceptionStruct);
}

void MethodCompiler::printOpcode(TInstruction instruction)
{
    switch (instruction.high) {
        // TODO Boundary checks against container's real size
        case opcode::pushInstance:    printf("doPushInstance %d\n", instruction.low);  break;
        case opcode::pushArgument:    printf("doPushArgument %d\n", instruction.low);  break;
        case opcode::pushTemporary:   printf("doPushTemporary %d\n", instruction.low); break;
        case opcode::pushLiteral:     printf("doPushLiteral %d\n", instruction.low);   break;
        case opcode::pushConstant:    printf("doPushConstant %d\n", instruction.low);  break;
        case opcode::pushBlock:       printf("doPushBlock %d\n", instruction.low);     break;
        
        case opcode::assignTemporary: printf("doAssignTemporary %d\n", instruction.low); break;
        case opcode::assignInstance:  printf("doAssignInstance %d\n", instruction.low);  break; // TODO checkRoot
        
        case opcode::markArguments:   printf("doMarkArguments %d\n", instruction.low); break;
        
        case opcode::sendUnary:       printf("doSendUnary\n");     break;
        case opcode::sendBinary:      printf("doSendBinary\n");    break;
        case opcode::sendMessage:     printf("doSendMessage\n");   break;
        
        case opcode::doSpecial:       printf("doSpecial\n");       break;
        case opcode::doPrimitive:     printf("doPrimitive\n");     break;
        
        default:
            fprintf(stderr, "JIT: Unknown opcode %d\n", instruction.high);
    }
}

void MethodCompiler::doPushInstance(TJITContext& jit)
{
    // Self is interpreted as object array.
    // Array elements are instance variables

    uint8_t index = jit.instruction.low;
    jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadInstance, index));
}

void MethodCompiler::doPushArgument(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadArgument, index));
}

void MethodCompiler::doPushTemporary(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadTemporary, index));
}

void MethodCompiler::doPushLiteral(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadLiteral, index));
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
            constantValue       = jit.builder->CreateIntToPtr(integerValue, m_baseTypes.object->getPointerTo());
            
            std::ostringstream ss;
            ss << "const" << (uint32_t) constant << ".";
            constantValue->setName(ss.str());
        } break;
        
        case pushConstants::nil:         constantValue = m_globals.nilObject;   break;
        case pushConstants::trueObject:  constantValue = m_globals.trueObject;  break;
        case pushConstants::falseObject: constantValue = m_globals.falseObject; break;
        
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
    
    TJITContext blockContext(this, jit.method, jit.callingContext);
    blockContext.bytePointer = jit.bytePointer;
    
    // Creating block function named Class>>method@offset
    const uint16_t blockOffset = jit.bytePointer;
    std::ostringstream ss;
    ss << jit.method->klass->name->toString() + ">>" + jit.method->name->toString() << "@" << blockOffset; //currentOffset;
    std::string blockFunctionName = ss.str();
    
    // outs() << "Creating block function "  << blockFunctionName << "\n";
    
    std::vector<Type*> blockParams;
    blockParams.push_back(m_baseTypes.block->getPointerTo()); // block object with context information
    
    FunctionType* blockFunctionType = FunctionType::get(
        m_baseTypes.object->getPointerTo(), // block return value
        blockParams,               // parameters
        false                      // we're not dealing with vararg
    );
    blockContext.function = cast<Function>(m_JITModule->getOrInsertFunction(blockFunctionName, blockFunctionType));
    blockContext.function->setGC("shadow-stack");
    m_blockFunctions[blockFunctionName] = blockContext.function;
    
    // Creating the basic block and inserting it into the function
    blockContext.preamble = BasicBlock::Create(m_JITModule->getContext(), "blockPreamble", blockContext.function);
    blockContext.builder = new IRBuilder<>(blockContext.preamble);
    writePreamble(blockContext, /*isBlock*/ true);
    scanForBranches(blockContext, newBytePointer - jit.bytePointer);
    
    BasicBlock* blockBody = BasicBlock::Create(m_JITModule->getContext(), "blockBody", blockContext.function);
    blockContext.builder->CreateBr(blockBody);
    blockContext.builder->SetInsertPoint(blockBody);
    
    writeFunctionBody(blockContext, newBytePointer - jit.bytePointer);

    // Create block object and fill it with context information
    Value* args[] = {
        jit.getCurrentContext(),                   // creatingContext
        jit.builder->getInt8(jit.instruction.low), // arg offset
        jit.builder->getInt16(blockOffset)         // bytePointer
    };
    Value* blockObject = jit.builder->CreateCall(m_runtimeAPI.createBlock, args);
    blockObject = jit.builder->CreateBitCast(blockObject, m_baseTypes.object->getPointerTo());
    blockObject->setName("block.");
    jit.bytePointer = newBytePointer;
    
    Value* blockHolder = protectPointer(jit, blockObject);
    jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, blockHolder));

    // Running optimization passes on a block function
    JITRuntime::Instance()->optimizeFunction(blockContext.function);
}

void MethodCompiler::doAssignTemporary(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    Value*  value = jit.lastValue();
    IRBuilder<>& builder = * jit.builder;
    
    Value* context = jit.getCurrentContext();
    
    Value* ptemps    = builder.CreateStructGEP(context, 3); // temporaries
    Value* temps     = builder.CreateLoad(ptemps);
    Value* pobject   = builder.CreateBitCast(temps, m_baseTypes.object->getPointerTo());
    builder.CreateCall3(m_baseFunctions.TObject__setField, pobject, builder.getInt32(index), value);
}

void MethodCompiler::doAssignInstance(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    Value*  value = jit.lastValue();
    IRBuilder<>& builder = * jit.builder;
    
    Value* self  = jit.getSelf();
    Value* fieldPointer = builder.CreateCall3(m_baseFunctions.TObject__setField, self, builder.getInt32(index), value);
    
    builder.CreateCall2(m_runtimeAPI.checkRoot, value, fieldPointer);
}

void MethodCompiler::doMarkArguments(TJITContext& jit)
{
    // Here we need to create the arguments array from the values on the stack
    uint8_t argumentsCount = jit.instruction.low;
    
    // FIXME Probably we may unroll the arguments array and pass the values directly.
    //       However, in some cases this may lead to additional architectural problems.
    Value* argumentsObject    = createArray(jit, argumentsCount);
    Value* argumentsFields    = jit.builder->CreateCall(m_baseFunctions.TObject__getFields, argumentsObject);
    
    // Filling object with contents
    uint8_t index = argumentsCount;
    while (index > 0) {
        Value* value = jit.popValue();
        Value* elementPtr = jit.builder->CreateGEP(argumentsFields, jit.builder->getInt32(--index));
        jit.builder->CreateStore(value, elementPtr);
    }
    
    Value* argumentsArray = jit.builder->CreateBitCast(argumentsObject, m_baseTypes.objectArray->getPointerTo());
    Value* argsHolder = protectPointer(jit, argumentsArray);
    argsHolder->setName("pArgs.");
    jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, argsHolder));
}

void MethodCompiler::doSendUnary(TJITContext& jit)
{
    Value* value     = jit.popValue();
    Value* condition = 0;
    
    switch ((unaryMessage::Opcode) jit.instruction.low) {
        case unaryMessage::isNil:  condition = jit.builder->CreateICmpEQ(value, m_globals.nilObject, "isNil.");  break;
        case unaryMessage::notNil: condition = jit.builder->CreateICmpNE(value, m_globals.nilObject, "notNil."); break;
        
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
    Value*    rightIsInt  = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, rightValue);
    Value*    leftIsInt   = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, leftValue);
    Value*    isSmallInts = jit.builder->CreateAnd(rightIsInt, leftIsInt);
    
    BasicBlock* integersBlock   = BasicBlock::Create(m_JITModule->getContext(), "asIntegers.", jit.function);
    BasicBlock* sendBinaryBlock = BasicBlock::Create(m_JITModule->getContext(), "asObjects.",  jit.function);
    BasicBlock* resultBlock     = BasicBlock::Create(m_JITModule->getContext(), "result.",     jit.function);
    
    // Linking pop-chain within the current logical block
    jit.basicBlockContexts[resultBlock].referers.insert(jit.builder->GetInsertBlock());
    
    // Dpending on the contents we may either do the integer operations
    // directly or create a send message call using operand objects
    jit.builder->CreateCondBr(isSmallInts, integersBlock, sendBinaryBlock);
    
    // Now the integers part
    jit.builder->SetInsertPoint(integersBlock);
    Value*    rightInt     = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, rightValue);
    Value*    leftInt      = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, leftValue);
    
    Value* intResult       = 0;  // this will be an immediate operation result
    Value* intResultObject = 0; // this will be actual object to return
    switch ((binaryMessage::Opcode) opcode) {
        case binaryMessage::smallIntLess    : intResult = jit.builder->CreateICmpSLT(leftInt, rightInt); break; // operator <
        case binaryMessage::smallIntLessOrEq: intResult = jit.builder->CreateICmpSLE(leftInt, rightInt); break; // operator <=
        case binaryMessage::smallIntAdd     : intResult = jit.builder->CreateAdd(leftInt, rightInt);     break; // operator +
        default:
            fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", opcode);
    }
    
    // Checking which operation was performed and
    // processing the intResult object in the proper way
    if (opcode == 2) {
        // Result of + operation will be number.
        // We need to create TInteger value and cast it to the pointer
        
        // Interpreting raw integer value as a pointer
        Value*  smalltalkInt = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult, "intAsPtr.");
        intResultObject = jit.builder->CreateIntToPtr(smalltalkInt, m_baseTypes.object->getPointerTo());
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

    // Creation of argument array may cause the GC which will break the arguments
    // We need to temporarily store them in a safe place
    Value* leftValueHolder  = protectPointer(jit, leftValue);
    Value* rightValueHolder = protectPointer(jit, rightValue);
    
    // Now creating the argument array
    Value* argumentsObject  = createArray(jit, 2);
    Value* argFields        = jit.builder->CreateCall(m_baseFunctions.TObject__getFields, argumentsObject);
    
    Value* element0Ptr = jit.builder->CreateGEP(argFields, jit.builder->getInt32(0));
    Value* restoredLeftValue = jit.builder->CreateLoad(leftValueHolder);
    jit.builder->CreateStore(restoredLeftValue, element0Ptr);
    
    Value* element1Ptr = jit.builder->CreateGEP(argFields, jit.builder->getInt32(1));
    Value* restoredRightValue = jit.builder->CreateLoad(rightValueHolder);
    jit.builder->CreateStore(restoredRightValue, element1Ptr);
    
    Value* argumentsArray    = jit.builder->CreateBitCast(argumentsObject, m_baseTypes.objectArray->getPointerTo());
    Value* sendMessageArgs[] = {
        jit.getCurrentContext(), // calling context
        m_globals.binarySelectors[jit.instruction.low],
        argumentsArray,
        
        // default receiver class
        ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()) //inttoptr 0 works fine too
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
    PHINode* phi = jit.builder->CreatePHI(m_baseTypes.object->getPointerTo(), 2, "phi.");
    phi->addIncoming(intResultObject, integersBlock);
    phi->addIncoming(sendMessageResult, sendBinaryBlock);
    
    Value* resultHolder = protectPointer(jit, phi);
    jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
}

void MethodCompiler::doSendMessage(TJITContext& jit)
{
    Value* arguments = jit.popValue();
    
    // First of all we need to get the actual message selector
    Value* selectorObject  = jit.getLiteral(jit.instruction.low);
    Value* messageSelector = jit.builder->CreateBitCast(selectorObject, m_baseTypes.symbol->getPointerTo());
    
    std::ostringstream ss;
    ss << "#" << jit.method->literals->getField(jit.instruction.low)->toString() << ".";
    messageSelector->setName(ss.str());
    
    // Forming a message parameters
    Value* sendMessageArgs[] = {
        jit.getCurrentContext(), // calling context
        messageSelector,         // selector
        arguments,               // message arguments
        
        // default receiver class
        ConstantPointerNull::get(m_baseTypes.klass->getPointerTo())
    };
    
    Value* result = 0;
    if (jit.methodHasBlockReturn) {
        // Creating basic block that will be branched to on normal invoke
        BasicBlock* nextBlock = BasicBlock::Create(m_JITModule->getContext(), "next.", jit.function);
        
        // Linking pop-chain within the current logical block
        jit.basicBlockContexts[nextBlock].referers.insert(jit.builder->GetInsertBlock());
        
        // Performing a function invoke
        result = jit.builder->CreateInvoke(m_runtimeAPI.sendMessage, nextBlock, jit.exceptionLandingPad, sendMessageArgs);
        
        // Switching builder to new block
        jit.builder->SetInsertPoint(nextBlock);
    } else {
        // Just calling the function. No block switching is required
        result = jit.builder->CreateCall(m_runtimeAPI.sendMessage, sendMessageArgs);
    }
    
    Value* resultHolder = protectPointer(jit, result);
    jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
}

void MethodCompiler::doSpecial(TJITContext& jit)
{
    TByteObject& byteCodes = * jit.method->byteCodes;
    uint8_t opcode = jit.instruction.low;
    
    BasicBlock::iterator iPreviousInst = jit.builder->GetInsertPoint();
    if (iPreviousInst != jit.builder->GetInsertBlock()->begin())
        --iPreviousInst;
    
    switch (opcode) {
        case special::selfReturn:
            if (! iPreviousInst->isTerminator())
                jit.builder->CreateRet(jit.getSelf());
            break;
        
        case special::stackReturn:
            if ( !iPreviousInst->isTerminator() && jit.hasValue() )
                jit.builder->CreateRet(jit.popValue());
            break;
        
        case special::blockReturn:
            if ( !iPreviousInst->isTerminator() && jit.hasValue()) {
                // Peeking the return value from the stack
                Value* value = jit.popValue();
                
                // Loading the target context information
                Value* blockContext = jit.builder->CreateBitCast(jit.getCurrentContext(), m_baseTypes.block->getPointerTo());
                Value* creatingContextPtr = jit.builder->CreateStructGEP(blockContext, 2);
                Value* targetContext      = jit.builder->CreateLoad(creatingContextPtr);
                
                // Emitting the TBlockReturn exception
                jit.builder->CreateCall2(m_runtimeAPI.emitBlockReturn, value, targetContext);
                
                // This will never be called
                jit.builder->CreateUnreachable();
            }
            break;
        
        case special::duplicate:
            // FIXME Duplicate the TStackValue, not the result
            outs() << "dup used!\n";
            jit.pushValue(jit.lastValue());
            break;
        
        case special::popTop:
            if (jit.hasValue())
                jit.popValue();
            break;
        
        case special::branch: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer += 2; // skipping the branch offset data
            
            if (!iPreviousInst->isTerminator()) {
                // Finding appropriate branch target
                // from the previously stored basic blocks
                BasicBlock* target = m_targetToBlockMap[targetOffset];
                jit.builder->CreateBr(target);
                
                // Updating block referers
                jit.basicBlockContexts[target].referers.insert(jit.builder->GetInsertBlock());
            }
        } break;
        
        case special::branchIfTrue:
        case special::branchIfFalse: {
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
                Value* boolObject = (opcode == special::branchIfTrue) ? m_globals.trueObject : m_globals.falseObject;
                Value* condition  = jit.popValue();
                Value* boolValue  = jit.builder->CreateICmpEQ(condition, boolObject);
                jit.builder->CreateCondBr(boolValue, targetBlock, skipBlock);
                
                // Updating referers
                jit.basicBlockContexts[targetBlock].referers.insert(jit.builder->GetInsertBlock());
                jit.basicBlockContexts[skipBlock].referers.insert(jit.builder->GetInsertBlock());
                
                // Switching to a newly created block
                jit.builder->SetInsertPoint(skipBlock);
            }
        } break;
        
        case special::sendToSuper: {
            Value* argsObject        = jit.popValue();
            Value* arguments         = jit.builder->CreateBitCast(argsObject, m_baseTypes.objectArray->getPointerTo());
            
            uint32_t literalIndex    = byteCodes[jit.bytePointer++];
            Value*   selectorObject  = jit.getLiteral(literalIndex);
            Value*   messageSelector = jit.builder->CreateBitCast(selectorObject, m_baseTypes.symbol->getPointerTo());
            
            Value* currentClass      = jit.getMethodClass();
            Value* parentClassPtr    = jit.builder->CreateStructGEP(currentClass, 2);
            Value* parentClass       = jit.builder->CreateLoad(parentClassPtr);
            
            Value* sendMessageArgs[] = {
                jit.getCurrentContext(),     // calling context
                messageSelector, // selector
                arguments,       // message arguments
                parentClass      // receiver class
            };
            
            Value* result = jit.builder->CreateCall(m_runtimeAPI.sendMessage, sendMessageArgs);
            Value* resultHolder = protectPointer(jit, result);
            jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
        } break;
        
        //case SmalltalkVM::breakpoint:
        // TODO breakpoint
        //break;
        
        default:
            printf("JIT: unknown special opcode %d\n", opcode);
    }
}

void MethodCompiler::doPrimitive(TJITContext& jit)
{
    uint32_t opcode = jit.method->byteCodes->getByte(jit.bytePointer++);
    //outs() << "Primitive opcode = " << opcode << "\n";
    
    Value* primitiveResult = 0;
    Value* primitiveCondition = jit.builder->getTrue();
    // br primitiveCondition, primitiveSucceeded, primitiveFailed
    // By default we use primitiveFailed BB as a trash code collecting block
    // llvm passes may eliminate primitiveFailed BB, becase br true, A, B -> br label A || If there is no other paths to B
    // But sometimes CFG of primitive may depend on primitiveCondition result (bulkReplace)
    // If your primitive may fail, you may use 2 ways:
    // 1) set br primitiveFailed
    // 2) bind primitiveCondition with any i1 result
    BasicBlock* primitiveSucceeded = BasicBlock::Create(m_JITModule->getContext(), "primitiveSucceeded", jit.function);
    BasicBlock* primitiveFailed = BasicBlock::Create(m_JITModule->getContext(), "primitiveFailed", jit.function);
    
    // Linking pop chain
    jit.basicBlockContexts[primitiveFailed].referers.insert(jit.builder->GetInsertBlock());
    
    switch (opcode) {
        case primitive::objectsAreEqual: {
            Value* object2 = jit.popValue();
            Value* object1 = jit.popValue();
            
            Value* result    = jit.builder->CreateICmpEQ(object1, object2);
            Value* boolValue = jit.builder->CreateSelect(result, m_globals.trueObject, m_globals.falseObject);
            
            primitiveResult = boolValue;
        } break;
        
        // TODO ioGetchar
        case primitive::ioPutChar: {
            Value* intObject = jit.popValue();
            Value* intValue  = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, intObject);
            Value* charValue = jit.builder->CreateTrunc(intValue, jit.builder->getInt8Ty());
            
            Function* putcharFunc = cast<Function>(m_JITModule->getOrInsertFunction("putchar", jit.builder->getInt32Ty(), jit.builder->getInt8Ty(), NULL));
            jit.builder->CreateCall(putcharFunc, charValue);
            
            primitiveResult = m_globals.nilObject;
        } break;
        
        case primitive::getClass:
        case primitive::getSize: {
            Value* object           = jit.popValue();
            Value* objectIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, object, "isSmallInt");
            
            BasicBlock* asSmallInt = BasicBlock::Create(m_JITModule->getContext(), "asSmallInt", jit.function);
            BasicBlock* asObject   = BasicBlock::Create(m_JITModule->getContext(), "asObject", jit.function);
            jit.builder->CreateCondBr(objectIsSmallInt, asSmallInt, asObject);
            
            jit.builder->SetInsertPoint(asSmallInt);
            Value* result = 0;
            if (opcode == primitive::getSize) {
                result = jit.builder->CreateCall(m_baseFunctions.newInteger, jit.builder->getInt32(0));
            } else {
                result = jit.builder->CreateBitCast(m_globals.smallIntClass, m_baseTypes.object->getPointerTo());
            }
            jit.builder->CreateRet(result);
            
            jit.builder->SetInsertPoint(asObject);
            if (opcode == primitive::getSize) {
                Value* size     = jit.builder->CreateCall(m_baseFunctions.TObject__getSize, object, "size");
                primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, size);
            } else {
                Value* klass = jit.builder->CreateCall(m_baseFunctions.TObject__getClass, object, "class");
                primitiveResult = jit.builder->CreateBitCast(klass, m_baseTypes.object->getPointerTo());
            }
        } break;
        
        case primitive::startNewProcess: { // 6
            /* ticks. unused */    jit.popValue();
            Value* processObject = jit.popValue();
            //TODO pushProcess ?
            Value* process       = jit.builder->CreateBitCast(processObject, m_baseTypes.process->getPointerTo());
            
            Function* executeProcess = m_JITModule->getFunction("executeProcess");
            Value*    processResult  = jit.builder->CreateCall(executeProcess, process);
            
            primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, processResult);
        } break;
        
        case primitive::allocateObject: { // 7
            Value* sizeObject  = jit.popValue();
            Value* klassObject = jit.popValue();
            Value* klass       = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());
            
            Value* size        = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, sizeObject, "size.");
            Value* slotSize    = jit.builder->CreateCall(m_baseFunctions.getSlotSize, size, "slotSize.");
            Value* args[]      = { klass, slotSize };
            
            Value* newInstance = jit.builder->CreateCall(m_runtimeAPI.newOrdinaryObject, args, "instance.");
            
            primitiveResult = newInstance;
        } break;
        
        case primitive::allocateByteArray: { // 20
            Value* sizeObject  = jit.popValue();
            Value* klassObject = jit.popValue();
            
            Value* klass       = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());
            Value* dataSize    = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, sizeObject, "dataSize.");
            
            Value* args[]      = { klass, dataSize };
            Value* newInstance = jit.builder->CreateCall(m_runtimeAPI.newBinaryObject, args, "instance.");

            primitiveResult = jit.builder->CreateBitCast(newInstance, m_baseTypes.object->getPointerTo() );
        } break;
        
        case primitive::cloneByteObject: { // 23
            Value* klassObject = jit.popValue();
            Value* original    = jit.popValue();
            Value* klass       = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());
            
            Value* dataSize = jit.builder->CreateCall(m_baseFunctions.TObject__getSize, original, "dataSize.");
            
            Value* args[]   = { klass, dataSize };
            Value* clone    = jit.builder->CreateCall(m_runtimeAPI.newBinaryObject, args, "clone.");
            
            Value* originalObject = jit.builder->CreateBitCast(original, m_baseTypes.object->getPointerTo());
            Value* cloneObject    = jit.builder->CreateBitCast(clone, m_baseTypes.object->getPointerTo());
            Value* sourceFields   = jit.builder->CreateCall(m_baseFunctions.TObject__getFields, originalObject);
            Value* destFields     = jit.builder->CreateCall(m_baseFunctions.TObject__getFields, cloneObject);
            
            Value* source       = jit.builder->CreateBitCast(sourceFields, jit.builder->getInt8PtrTy());
            Value* destination  = jit.builder->CreateBitCast(destFields, jit.builder->getInt8PtrTy());
            
            // Copying the data
            Value* copyArgs[] = {
                destination, 
                source,
                dataSize,
                jit.builder->getInt32(0), // no alignment
                jit.builder->getFalse()   // not volatile
            };
            
            Type* memcpyType[] = {jit.builder->getInt8PtrTy(), jit.builder->getInt8PtrTy(), jit.builder->getInt32Ty() };
            Function* memcpyIntrinsic = getDeclaration(m_JITModule, Intrinsic::memcpy, memcpyType);
            
            jit.builder->CreateCall(memcpyIntrinsic, copyArgs);
            
            primitiveResult = cloneObject;
        } break;
        
        case primitive::integerNew:
            primitiveResult = jit.popValue(); // TODO long integers
            break;
        
        case primitive::blockInvoke: { // 8
            Value* object = jit.popValue();
            Value* block  = jit.builder->CreateBitCast(object, m_baseTypes.block->getPointerTo());
            
            int32_t argCount = jit.instruction.low - 1;
            
            Value* blockAsContext   = jit.builder->CreateBitCast(block, m_baseTypes.context->getPointerTo());
            Value* blockTempsPtr    = jit.builder->CreateStructGEP(blockAsContext, 3);
            Value* blockTemps       = jit.builder->CreateLoad(blockTempsPtr);
            Value* blockTempsObject = jit.builder->CreateBitCast(blockTemps, m_baseTypes.object->getPointerTo());
            
            Value* fields    = jit.builder->CreateCall(m_baseFunctions.TObject__getFields, blockTempsObject);
            Value* tempsSize = jit.builder->CreateCall(m_baseFunctions.TObject__getSize, blockTempsObject, "tempsSize.");
            
            Value* argumentLocationPtr    = jit.builder->CreateStructGEP(block, 1);
            Value* argumentLocationField  = jit.builder->CreateLoad(argumentLocationPtr);
            Value* argumentLocationObject = jit.builder->CreateIntToPtr(argumentLocationField, m_baseTypes.object->getPointerTo());
            Value* argumentLocation       = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, argumentLocationObject, "argLocation.");
            
            BasicBlock* tempsChecked = BasicBlock::Create(m_JITModule->getContext(), "tempsChecked.", jit.function);
            
            //Checking the passed temps size TODO unroll stack
            Value* blockAcceptsArgCount = jit.builder->CreateSub(tempsSize, argumentLocation);
            Value* tempSizeOk = jit.builder->CreateICmpSLE(jit.builder->getInt32(argCount), blockAcceptsArgCount);
            jit.builder->CreateCondBr(tempSizeOk, tempsChecked, primitiveFailed);
            
            jit.basicBlockContexts[tempsChecked].referers.insert(jit.builder->GetInsertBlock());
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
            
            Value* args[] = { block, jit.getCurrentContext() };
            Value* result = jit.builder->CreateCall(m_runtimeAPI.invokeBlock, args);
            
            primitiveResult = result;
        } break;
        
        case primitive::throwError: { //19
            //19 primitive is very special. It raises exception, no code is reachable
            //after calling cxa_throw. But! Someone may add Smalltalk code after <19>
            //Thats why we have to create unconditional br to 'primitiveFailed'
            //to catch any generated code into that BB
            Value* contextPtr2Size = jit.builder->CreateTrunc(ConstantExpr::getSizeOf(m_baseTypes.context->getPointerTo()->getPointerTo()), jit.builder->getInt32Ty());
            Value* expnBuffer      = jit.builder->CreateCall(m_exceptionAPI.cxa_allocate_exception, contextPtr2Size);
            Value* expnTypedBuffer = jit.builder->CreateBitCast(expnBuffer, m_baseTypes.context->getPointerTo()->getPointerTo());
            jit.builder->CreateStore(jit.getCurrentContext(), expnTypedBuffer);
             
            Value* throwArgs[] = {
                expnBuffer,
                jit.builder->CreateBitCast(m_exceptionAPI.contextTypeInfo, jit.builder->getInt8PtrTy()),
                ConstantPointerNull::get(jit.builder->getInt8PtrTy())
            };
            
            jit.builder->CreateCall(m_exceptionAPI.cxa_throw, throwArgs);
            primitiveSucceeded->eraseFromParent();
            jit.builder->CreateBr(primitiveFailed);
            jit.builder->SetInsertPoint(primitiveFailed);
            return;
        } break;
        
        case primitive::arrayAt:       // 24
        case primitive::arrayAtPut: {  // 5
            Value* indexObject = jit.popValue();
            Value* arrayObject = jit.popValue();
            Value* valueObejct = (opcode == primitive::arrayAtPut) ? jit.popValue() : 0;
            
            BasicBlock* indexChecked = BasicBlock::Create(m_JITModule->getContext(), "indexChecked.", jit.function);
            
            //Checking whether index is Smallint //TODO jump to primitiveFailed if not
            Value* indexIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, indexObject);
            
            Value* index       = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, indexObject);
            Value* actualIndex = jit.builder->CreateSub(index, jit.builder->getInt32(1));
            
            //Checking boundaries
            Value* arraySize   = jit.builder->CreateCall(m_baseFunctions.TObject__getSize, arrayObject);
            Value* indexGEZero = jit.builder->CreateICmpSGE(actualIndex, jit.builder->getInt32(0));
            Value* indexLTSize = jit.builder->CreateICmpSLT(actualIndex, arraySize);
            Value* boundaryOk  = jit.builder->CreateAnd(indexGEZero, indexLTSize);
            
            Value* indexOk = jit.builder->CreateAnd(indexIsSmallInt, boundaryOk);
            jit.builder->CreateCondBr(indexOk, indexChecked, primitiveFailed);
            jit.builder->SetInsertPoint(indexChecked);
            
            Value* fields    = jit.builder->CreateCall(m_baseFunctions.TObject__getFields, arrayObject);
            Value* fieldPtr  = jit.builder->CreateGEP(fields, actualIndex);
            
            if (opcode == primitive::arrayAtPut) {
                jit.builder->CreateStore(valueObejct, fieldPtr);
                primitiveResult = arrayObject; // valueObejct;
            } else {
                primitiveResult = jit.builder->CreateLoad(fieldPtr);
            }
        } break;
        
        case primitive::stringAt:       // 21
        case primitive::stringAtPut: {  // 22
            Value* indexObject  = jit.popValue();
            Value* stringObject = jit.popValue();
            Value* valueObejct  = (opcode == primitive::stringAtPut) ? jit.popValue() : 0;
            
            BasicBlock* indexChecked = BasicBlock::Create(m_JITModule->getContext(), "indexChecked.", jit.function);
            
            //Checking whether index is Smallint //TODO jump to primitiveFailed if not
            Value* indexIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, indexObject);
            
            // Acquiring integer value of the index (from the smalltalk's TInteger)
            Value*    index    = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, indexObject);
            Value* actualIndex = jit.builder->CreateSub(index, jit.builder->getInt32(1));
            
            //Checking boundaries
            Value* stringSize  = jit.builder->CreateCall(m_baseFunctions.TObject__getSize, stringObject);
            Value* indexGEZero = jit.builder->CreateICmpSGE(actualIndex, jit.builder->getInt32(0));
            Value* indexLTSize = jit.builder->CreateICmpSLT(actualIndex, stringSize);
            Value* boundaryOk  = jit.builder->CreateAnd(indexGEZero, indexLTSize);
            
            Value* indexOk = jit.builder->CreateAnd(indexIsSmallInt, boundaryOk, "indexOk.");
            jit.builder->CreateCondBr(indexOk, indexChecked, primitiveFailed);
            jit.builder->SetInsertPoint(indexChecked);
            
            // Getting access to the actual indexed byte location
            Value* fields    = jit.builder->CreateCall(m_baseFunctions.TObject__getFields, stringObject);
            Value* bytes     = jit.builder->CreateBitCast(fields, jit.builder->getInt8PtrTy());
            Value* bytePtr   = jit.builder->CreateGEP(bytes, actualIndex);
            
            if (opcode == primitive::stringAtPut) {
                // Popping new value from the stack, getting actual integral value from the TInteger
                // then shrinking it to the 1 byte representation and inserting into the pointed location
                Value* valueInt = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, valueObejct);
                Value* byte = jit.builder->CreateTrunc(valueInt, jit.builder->getInt8Ty());
                jit.builder->CreateStore(byte, bytePtr); 
                
                primitiveResult = stringObject;
            } else {
                // Loading string byte pointed by the pointer,
                // expanding it to the 4 byte integer and returning
                // as TInteger value
                
                Value* byte = jit.builder->CreateLoad(bytePtr);
                Value* expandedByte = jit.builder->CreateZExt(byte, jit.builder->getInt32Ty());
                primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, expandedByte);
            }
        } break;
        
        
        case primitive::smallIntAdd:        // 10
        case primitive::smallIntDiv:        // 11
        case primitive::smallIntMod:        // 12
        case primitive::smallIntLess:       // 13
        case primitive::smallIntEqual:      // 14
        case primitive::smallIntMul:        // 15
        case primitive::smallIntSub:        // 16
        case primitive::smallIntBitOr:      // 36
        case primitive::smallIntBitAnd:     // 37
        case primitive::smallIntBitShift: { // 39
            Value* rightObject = jit.popValue();
            Value* leftObject  = jit.popValue();
            
            Value* rightIsInt  = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, rightObject);
            Value* leftIsInt   = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, leftObject);
            Value* isSmallInts = jit.builder->CreateAnd(rightIsInt, leftIsInt);
            
            BasicBlock* areInts = BasicBlock::Create(m_JITModule->getContext(), "areInts.", jit.function);
            jit.builder->CreateCondBr(isSmallInts, areInts, primitiveFailed);
            
            jit.builder->SetInsertPoint(areInts);
            Value* rightOperand = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, rightObject);
            Value* leftOperand  = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, leftObject);
            
            switch(opcode) { //FIXME move to function
                case primitive::smallIntAdd: {
                    Value* intResult = jit.builder->CreateAdd(leftOperand, rightOperand);
                    //FIXME overflow
                    primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
                } break;
                case primitive::smallIntDiv: {
                    Value*      isZero = jit.builder->CreateICmpEQ(rightOperand, jit.builder->getInt32(0));
                    BasicBlock* divBB  = BasicBlock::Create(m_JITModule->getContext(), "div.", jit.function);
                    jit.builder->CreateCondBr(isZero, primitiveFailed, divBB);
                    
                    jit.builder->SetInsertPoint(divBB);
                    Value* intResult = jit.builder->CreateSDiv(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
                } break;
                case primitive::smallIntMod: {
                    Value*      isZero = jit.builder->CreateICmpEQ(rightOperand, jit.builder->getInt32(0));
                    BasicBlock* modBB  = BasicBlock::Create(m_JITModule->getContext(), "mod.", jit.function);
                    jit.builder->CreateCondBr(isZero, primitiveFailed, modBB);
                    
                    jit.builder->SetInsertPoint(modBB);
                    Value* intResult = jit.builder->CreateSRem(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
                } break;
                case primitive::smallIntLess: {
                    Value* condition = jit.builder->CreateICmpSLT(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
                } break;
                case primitive::smallIntEqual: {
                    Value* condition = jit.builder->CreateICmpEQ(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
                } break;
                case primitive::smallIntMul: {
                    Value* intResult = jit.builder->CreateMul(leftOperand, rightOperand);
                    //FIXME overflow
                    primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
                } break;
                case primitive::smallIntSub: {
                    Value* intResult = jit.builder->CreateSub(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
                } break;
                case primitive::smallIntBitOr: {
                    Value* intResult = jit.builder->CreateOr(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
                } break;
                case primitive::smallIntBitAnd: {
                    Value* intResult = jit.builder->CreateAnd(leftOperand, rightOperand);
                    primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
                } break;
                case primitive::smallIntBitShift: {
                    BasicBlock* shiftRightBB  = BasicBlock::Create(m_JITModule->getContext(), ">>", jit.function);
                    BasicBlock* shiftLeftBB   = BasicBlock::Create(m_JITModule->getContext(), "<<", jit.function);
                    BasicBlock* shiftResultBB = BasicBlock::Create(m_JITModule->getContext(), "shiftResult", jit.function);
                    
                    Value* rightIsNeg = jit.builder->CreateICmpSLT(rightOperand, jit.builder->getInt32(0));
                    jit.builder->CreateCondBr(rightIsNeg, shiftRightBB, shiftLeftBB);
                    
                    jit.builder->SetInsertPoint(shiftRightBB);
                    Value* rightOperandNeg  = jit.builder->CreateNeg(rightOperand);
                    Value* shiftRightResult = jit.builder->CreateAShr(leftOperand, rightOperandNeg);
                    jit.builder->CreateBr(shiftResultBB);
                    
                    jit.builder->SetInsertPoint(shiftLeftBB);
                    Value* shiftLeftResult = jit.builder->CreateShl(leftOperand, rightOperand);
                    Value* shiftLeftFailed = jit.builder->CreateICmpSGT(leftOperand, shiftLeftResult);
                    jit.builder->CreateCondBr(shiftLeftFailed, primitiveFailed, shiftResultBB);
                    
                    jit.builder->SetInsertPoint(shiftResultBB);
                    PHINode* phi = jit.builder->CreatePHI(jit.builder->getInt32Ty(), 2);
                    phi->addIncoming(shiftRightResult, shiftRightBB);
                    phi->addIncoming(shiftLeftResult, shiftLeftBB);
                    
                    primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, phi);
                } break;
            }
        } break;
        
        case primitive::bulkReplace: {
            Value* destination            = jit.popValue();
            Value* sourceStartOffset      = jit.popValue();
            Value* source                 = jit.popValue();
            Value* destinationStopOffset  = jit.popValue();
            Value* destinationStartOffset = jit.popValue();
            
            Value* arguments[]  = {
                destination,
                destinationStartOffset,
                destinationStopOffset,
                source,
                sourceStartOffset
            };
            
            Value* isSucceeded  = jit.builder->CreateCall(m_runtimeAPI.bulkReplace, arguments, "ok.");
            primitiveResult = destination;
            primitiveCondition = isSucceeded;
        } break;
        
        default:
            outs() << "JIT: Unknown primitive code " << opcode << "\n";
    }
    
    // Linking pop chain
    jit.basicBlockContexts[primitiveSucceeded].referers.insert(jit.builder->GetInsertBlock());
    
    jit.builder->CreateCondBr(primitiveCondition, primitiveSucceeded, primitiveFailed);
    jit.builder->SetInsertPoint(primitiveSucceeded);
    
    jit.builder->CreateRet(primitiveResult);
    jit.builder->SetInsertPoint(primitiveFailed);
    
    jit.pushValue(m_globals.nilObject);
}