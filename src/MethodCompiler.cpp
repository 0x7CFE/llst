/*
 *    MethodCompiler.cpp
 *
 *    Implementation of MethodCompiler class which is used to
 *    translate smalltalk bytecodes to LLVM IR code
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.3
 *
 *    LLST is
 *        Copyright (C) 2012-2015 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2015 by Roman Proskuryakov <humbug@deeptown.org>
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

#include <llvm/IR/Intrinsics.h>
#include <llvm/Analysis/Verifier.h>

#include <stdarg.h>
#include <iostream>
#include <sstream>
#include <opcodes.h>
#include <analysis.h>
#include <visualization.h>

using namespace llvm;

Value* MethodCompiler::TJITContext::getLiteral(uint32_t index)
{
    Module* jitModule = JITRuntime::Instance()->getModule();
    Function* getLiteralFromContext = jitModule->getFunction("getLiteralFromContext");

    Value* context = getCurrentContext();
    CallInst* literal = builder->CreateCall2(getLiteralFromContext, context, builder->getInt32(index));

    std::ostringstream ss;
    ss << "lit" << index << ".";
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

Function* MethodCompiler::createFunction(TMethod* method)
{
    Type* methodParams[] = { m_baseTypes.context->getPointerTo() };
    FunctionType* functionType = FunctionType::get(
        m_baseTypes.object->getPointerTo(), // the type of function result
        methodParams,                       // parameters
        false                               // we're not dealing with vararg
    );

    std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
    Function* function = cast<Function>( m_JITModule->getOrInsertFunction(functionName, functionType));
    function->setCallingConv(CallingConv::C); //Anyway C-calling conversion is default
    function->setGC("shadow-stack");
    function->addFnAttr(Attribute::InlineHint);
//     function->addFnAttr(Attribute::AlwaysInline);
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

Value* MethodCompiler::protectProducerNode(TJITContext& jit, st::ControlNode* node, Value* value)
{
    if (shouldProtectProducer(jit.currentNode))
        return protectPointer(jit, value);
    else
        return value; // return value as is
}

class Detector {
public:
    Detector(st::PathVerifier& verifier) : m_verifier(verifier), m_detected(false) {}

    bool isDetected() const { return m_detected; }

    st::GraphWalker::TVisitResult checkNode(st::ControlNode* node) {
        if (st::InstructionNode* const candidate = node->cast<st::InstructionNode>()) {
            if (candidate->getInstruction().mayCauseGC()) {
                outs() << "Detector noticed node " << candidate->getIndex() << " that may cause GC\n";

                m_detected = true;
                return st::GraphWalker::vrStopWalk;

                /* / Walking through the nodes seeking for consumer
                // If it is found then candidate node may affect
                // the consumer. Procucer should be protected.
                m_verifier.run(candidate);

                if (m_verifier.isVerified()) {
                    // We found a node that may cause GC and it is really
                    // on the path between procducer and one of the consumers.
                    // We've just proved that the value should be protected.

                    m_detected = true;
                    return st::GraphWalker::vrStopWalk;

                } else {
                    // Node may cause GC but control flow will never reach any of tracked consumers.
                    // This means that we may safely ignore this node and all it's subpaths.

                    return st::GraphWalker::vrSkipPath;
                } */
            }
        } else if (st::PhiNode* const phi = node->cast<st::PhiNode>()) {
            // Phi node may not cause gc, protects it's value separately
            // and do not have outgoing edges that we may traverse.

            return st::GraphWalker::vrSkipPath;
        } else {
            assert(false);
        }

        return st::GraphWalker::vrKeepWalking;
    }

private:
    st::PathVerifier& m_verifier;
    bool m_detected;
};

class Walker : public st::GraphWalker {
public:
    Walker(Detector& detector) : m_detector(detector) {}

private:
    Detector& m_detector;
    virtual TVisitResult visitNode(st::ControlNode* node) { return m_detector.checkNode(node); }
};

bool MethodCompiler::methodAllocatesMemory(TJITContext& jit)
{
    class GCDetector : public st::ForwardWalker {
    public:
        GCDetector() : m_detected(false) {}

        virtual TVisitResult visitNode(st::ControlNode* node) {
            if (st::InstructionNode* const candidate = node->cast<st::InstructionNode>()) {
                if (candidate->getInstruction().mayCauseGC()) {
                    m_detected = true;
                    return st::GraphWalker::vrStopWalk;
                }
            }

            return st::GraphWalker::vrKeepWalking;
        }

        bool isDetected() const { return m_detected; }

    private:
        bool m_detected;
    };

    GCDetector detector;
    detector.run((*jit.controlGraph->begin())->getEntryPoint());

    return detector.isDetected();
}

bool MethodCompiler::shouldProtectProducer(st::ControlNode* producer)
{
    // We should protect the value by holder if consumer of this value is far away.
    // By far away we mean that it crosses the barrier of potential garbage collection.
    // For example if value is consumed right next to the point it was produced, then
    // protection is not neccessary. Otherwise if there are memory allocation points in
    // between then value protection is a must.
    //
    // In order to find out whether value protection is required, we trace the possible
    // control- and data-flow paths and check what instructions are on the way between
    // value producer and all potential consumers. If there is at least one dangerous
    // operation on any of possible execution paths then the whole value should be protected.

    const st::TNodeSet& consumers = producer->getConsumers();

    // In case of local domain reference we may apply fast check
    if (consumers.size() == 1) {
        st::ControlNode* const consumer = *consumers.begin();

        if (producer->getDomain() == consumer->getDomain()) {
            // Walking through the domain searching for dangerous nodes

            assert(producer->getOutEdges().size() == 1);
            st::ControlNode* node = *producer->getOutEdges().begin();

            while (true) {
                if (node == consumer) {
//                     outs() << "Producer " << producer->getIndex() << " is safe and do not need a protection (1)\n";

                    return false;
                }

                if (st::InstructionNode* const candidate = node->cast<st::InstructionNode>()) {
                    if (candidate->getInstruction().mayCauseGC()) {
                        outs() << "Producer " << producer->getIndex()
                               << " should be protected because node "
                               << candidate->getIndex() << " may cause GC\n";

                        return true;
                    }
                } else {
                    // There should be instruction nodes only
                    assert(false);
                }

                assert(node->getOutEdges().size() == 1);
                node = *node->getOutEdges().begin();
            }
        }
    }

    // Ok, it seem that fast lookup had failed. It means that we're dealing with
    // a complex case that affects several domains and probably phi nodes.
    // We need to perform a generic lookup walking through the whole graph.

    st::PathVerifier verifier(consumers);
    verifier.addStopNode(producer);

    Detector detector(verifier);

    Walker walker(detector);
    walker.addStopNode(producer);
    walker.addStopNodes(consumers);

    // Running detector for all incoming paths to consumers originating from producer.
    // Walker will accumulate all safe paths and will not traverse any safe node twice.

    for (st::TNodeSet::iterator iConsumer = consumers.begin(); iConsumer != consumers.end(); ++iConsumer) {
        st::ControlNode* const consumer = *iConsumer;
        walker.run(consumer, st::GraphWalker::wdBackward);

        if (detector.isDetected()) {
            outs() << "Producer " << producer->getIndex()
                   << " should be protected because detector says that it is required\n";

            return true;
        }
    }

//     outs() << "Producer " << producer->getIndex() << " is safe and do not need a protection (2)\n";
    return false;

    // Changing direction to forward and performing last check starting from procucer.
    // We need to reset the stop list because now we'll use differennt edges.
//     walker.resetStopNodes();
//
//     // When performing a forward run we need to end at consumers
//     walker.addStopNodes(consumers);
//     walker.run(producer, st::GraphWalker::wdForward);

//     return detector.isDetected();
}

void MethodCompiler::writePreamble(TJITContext& jit, bool isBlock)
{
    Value* context = 0;

    if (! isBlock) {
        // This is a regular function
        context = jit.function->arg_begin();
        context->setName("context");
    } else {
        // This is a block function
        Value* blockContext = jit.function->arg_begin();
        blockContext->setName("blockContext");

        context = jit.builder->CreateBitCast(blockContext, m_baseTypes.context->getPointerTo());
    }
    context->setName("contextParameter");

    // Protecting the context holder
    jit.contextHolder = jit.methodAllocatesMemory ? protectPointer(jit, context) : context;
    jit.contextHolder->setName("pContext");

    // Storing self pointer
    Value* pargs     = jit.builder->CreateStructGEP(context, 2);
    Value* arguments = jit.builder->CreateLoad(pargs);
    Value* pobject   = jit.builder->CreateBitCast(arguments, m_baseTypes.object->getPointerTo());
    Value* self      = jit.builder->CreateCall2(m_baseFunctions.getObjectField, pobject, jit.builder->getInt32(0));
    jit.selfHolder   = jit.methodAllocatesMemory ? protectPointer(jit, self) : self;
    jit.selfHolder->setName("pSelf");
}

Value* MethodCompiler::TJITContext::getCurrentContext()
{
    if (methodAllocatesMemory)
        return builder->CreateLoad(contextHolder, "context.");
    else
        return contextHolder;
}

Value* MethodCompiler::TJITContext::getSelf()
{
    if (methodAllocatesMemory)
        return builder->CreateLoad(selfHolder, "self.");
    else
        return selfHolder;
}

bool MethodCompiler::scanForBlockReturn(TJITContext& jit, uint32_t byteCount/* = 0*/)
{
    // Here we trying to find out whether method code contains block return instruction.
    // This instruction is handled in a very different way than the usual opcodes.
    // Thus requires special handling. Block return is done by trowing an exception out of
    // the block containing it. Then it's catched by the method's code to perform a return.
    // In order not to bloat the code with unused try-catch code we're previously scanning
    // the method's code to ensure that try-catch is really needed. If it is not, we simply
    // skip its generation.

    st::BlockReturnDetector detector(jit.parsedMethod);
    detector.run();

    return detector.isBlockReturnFound();
}

void MethodCompiler::scanForBranches(TJITContext& jit, st::ParsedBytecode* source, uint32_t byteCount /*= 0*/)
{
    // Iterating over method's basic blocks and creating their representation in LLVM
    // Created blocks are collected in the m_targetToBlockMap map with bytecode offset as a key

    class Visitor : public st::BasicBlockVisitor {
    public:
        Visitor(TJITContext& jit, st::ParsedBytecode* source) : BasicBlockVisitor(source), m_jit(jit) { }

    private:
        virtual bool visitBlock(st::BasicBlock& basicBlock) {
            std::ostringstream offset;
            offset << "offset" << basicBlock.getOffset();

            MethodCompiler* compiler = m_jit.compiler; // self reference
            llvm::BasicBlock* newBlock = llvm::BasicBlock::Create(
                compiler->m_JITModule->getContext(),   // creating context
                offset.str(),                          // new block's name
                m_jit.function                         // method's function
            );

//             compiler->m_targetToBlockMap[basicBlock.getOffset()] = newBlock;
            basicBlock.setValue(newBlock);

            return true;
        }

        TJITContext& m_jit;
    };

    Visitor visitor(jit, source);
    visitor.run();
}

Value* MethodCompiler::createArray(TJITContext& jit, uint32_t elementsCount)
{
    TStackObject array = allocateStackObject(*jit.builder, sizeof(TObjectArray), elementsCount);
    // Instantinating new array object
    const uint32_t arraySize = sizeof(TObjectArray) + sizeof(TObject*) * elementsCount;
    jit.builder->CreateMemSet(
        array.objectSlot,           // destination address
        jit.builder->getInt8(0),    // fill with zeroes
        arraySize,                  // size of object slot
        0,                          // no alignment
        false                       // volatile operation
    );

    Value* arrayObject = jit.builder->CreateBitCast(array.objectSlot, m_baseTypes.object->getPointerTo());

    jit.builder->CreateCall2(m_baseFunctions.setObjectSize, arrayObject, jit.builder->getInt32(elementsCount));
    jit.builder->CreateCall2(m_baseFunctions.setObjectClass, arrayObject, m_globals.arrayClass);

    return arrayObject;
}

Function* MethodCompiler::compileMethod(TMethod* method, llvm::Function* methodFunction /*= 0*/, llvm::Value** contextHolder /*= 0*/)
{
    TJITContext  jit(this, method);

    // Creating the function named as "Class>>method" or using provided one
    jit.function = methodFunction ? methodFunction : createFunction(method);

    outs() << "Compiling " << jit.function->getName() << "\n";

//     {
//         std::ostringstream ss;
//         ss << "dots/" << jit.function->getName().data() << ".dot";
//         ControlGraphVisualizer vis(jit.controlGraph, ss.str());
//         vis.run();
//     }

    // Creating the preamble basic block and inserting it into the function
    // It will contain basic initialization code (args, temps and so on)
    jit.preamble = BasicBlock::Create(m_JITModule->getContext(), "preamble", jit.function);

    // Creating the instruction builder
    jit.builder = new IRBuilder<>(jit.preamble);

    // Checking whether method contains inline blocks that has blockReturn instruction.
    // If this is true we need to put an exception handler into the method and treat
    // all send message operations as invokes, not just simple calls
    jit.methodHasBlockReturn = scanForBlockReturn(jit);

    jit.methodAllocatesMemory = methodAllocatesMemory(jit);

    // Writing the function preamble and initializing
    // commonly used pointers such as method arguments or temporaries
    writePreamble(jit);
    if (contextHolder)
        *contextHolder = jit.contextHolder;

    // Writing exception handlers for the
    // correct operation of block return
    if (jit.methodHasBlockReturn)
        writeLandingPad(jit);

    // Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with
    // target bytecode offset as a key.
    scanForBranches(jit, jit.parsedMethod);

    // Switching builder context to the first basic block from the preamble
    BasicBlock* const body = jit.parsedMethod->getBasicBlockByOffset(0)->getValue(); // m_targetToBlockMap[0];
    assert(body);
    body->setName("offset0");

    jit.builder->SetInsertPoint(jit.preamble);
    jit.builder->CreateBr(body);

    // Resetting the builder to the body
    jit.builder->SetInsertPoint(body);

    // Processing the method's bytecodes
    writeFunctionBody(jit);

    // Cleaning up
    m_blockFunctions.clear();
//     m_targetToBlockMap.clear();

    outs() << "Done compiling method " << jit.function->getName() << "\n";

    return jit.function;
}

void MethodCompiler::writeFunctionBody(TJITContext& jit)
{
    class Visitor : public st::NodeVisitor {
    public:
        Visitor(TJITContext& jit) : st::NodeVisitor(jit.controlGraph), m_jit(jit) { }

    private:
        virtual bool visitDomain(st::ControlDomain& domain) {
            llvm::BasicBlock* newBlock = domain.getBasicBlock()->getValue();
            // llvm::BasicBlock* newBlock = m_jit.compiler->m_targetToBlockMap[domain.getBasicBlock()->getOffset()];

            newBlock->moveAfter(m_jit.builder->GetInsertBlock()); // for a pretty sequenced BB output
            m_jit.builder->SetInsertPoint(newBlock, newBlock->getFirstInsertionPt());

            return NodeVisitor::visitDomain(domain);
        }

        virtual bool visitNode(st::ControlNode& node) {
            m_jit.currentNode = node.cast<st::InstructionNode>();
            assert(m_jit.currentNode);

            m_jit.compiler->writeInstruction(m_jit);
            return NodeVisitor::visitNode(node);
        }

        virtual void nodesVisited() {
            BasicBlock* const lastBlock = m_jit.builder->GetInsertBlock();
            m_jit.currentNode->getDomain()->getBasicBlock()->setEndValue(lastBlock);
        }

        virtual void domainsVisited() {
            // Encoding incoming values of pending phi nodes

            TJITContext::TPhiList::iterator iPhi = m_jit.pendingPhiNodes.begin();
            for (; iPhi != m_jit.pendingPhiNodes.end(); ++iPhi) {
                st::PhiNode* const phi = *iPhi;

                // Phi should already be encoded, all we need is to fill the incoming list
                assert(phi->getValue());
                assert(phi->getPhiValue());

                m_jit.compiler->encodePhiIncomings(m_jit, phi);
            }
        }

    private:
        TJITContext& m_jit;
    };

    Visitor visitor(jit);
    visitor.run();
}

void MethodCompiler::writeInstruction(TJITContext& jit) {
    switch (jit.currentNode->getInstruction().getOpcode()) {
        // TODO Boundary checks against container's real size
        case opcode::pushInstance:      doPushInstance(jit);    break;
        case opcode::pushArgument:      doPushArgument(jit);    break;
        case opcode::pushTemporary:     doPushTemporary(jit);   break;
        case opcode::pushLiteral:       doPushLiteral(jit);     break;
        case opcode::pushConstant:      doPushConstant(jit);    break;

        case opcode::pushBlock:         doPushBlock(jit);       break; // FIXME

        case opcode::assignTemporary:   doAssignTemporary(jit); break;
        case opcode::assignInstance:    doAssignInstance(jit);  break;

        case opcode::markArguments:     doMarkArguments(jit);   break;
        case opcode::sendUnary:         doSendUnary(jit);       break;
        case opcode::sendBinary:        doSendBinary(jit);      break;
        case opcode::sendMessage:       doSendMessage(jit);     break;

        case opcode::doSpecial:         doSpecial(jit);         break;
        case opcode::doPrimitive:       doPrimitive(jit);       break;

        // Treating opcode 0 as NOP command
        // This is a temporary hack to solve bug #26
        // It is triggered by malformed method code
        // compiled by in-image soft compiler
        case 0: break;

        default:
            std::fprintf(stderr, "JIT: Invalid opcode %d at node %d in method %s\n",
                         jit.currentNode->getInstruction().getOpcode(),
                         jit.currentNode->getIndex(), jit.originMethod->name->toString().c_str());
    }
}

void MethodCompiler::writeLandingPad(TJITContext& jit)
{
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

void MethodCompiler::doPushInstance(TJITContext& jit)
{
    const uint8_t index = jit.currentNode->getInstruction().getArgument();
    Function* const getObjectField = m_JITModule->getFunction("getObjectField");

    // Self is interpreted as object array.
    // Array elements are instance variables.
    Value* const self  = jit.getSelf();
    Value* const field = jit.builder->CreateCall2(getObjectField, self, jit.builder->getInt32(index));

    std::ostringstream ss;
    ss << "field" << index << ".";
    field->setName(ss.str());

    Value* const holder = protectProducerNode(jit, jit.currentNode, field);
    setNodeValue(jit, jit.currentNode, holder);
}

void MethodCompiler::doPushArgument(TJITContext& jit)
{
    const uint8_t index = jit.currentNode->getInstruction().getArgument();

    if (index == 0) {
        // Optimizing self
        setNodeValue(jit, jit.currentNode, jit.selfHolder);
        return;
    }

    Function* const getArgFromContext = m_JITModule->getFunction("getArgFromContext");
    Value* const context  = jit.getCurrentContext();
    Value* const argument = jit.builder->CreateCall2(getArgFromContext, context, jit.builder->getInt32(index));

    std::ostringstream ss;
    ss << "arg" << index << ".";
    argument->setName(ss.str());

    Value* const holder = protectProducerNode(jit, jit.currentNode, argument);
    setNodeValue(jit, jit.currentNode, holder);
}

void MethodCompiler::doPushTemporary(TJITContext& jit)
{
    const uint8_t index = jit.currentNode->getInstruction().getArgument();

    Function* const getTempsFromContext = m_JITModule->getFunction("getTempsFromContext");
    Function* const getObjectField = m_JITModule->getFunction("getObjectField");

    Value* const context   = jit.getCurrentContext();
    Value* const temps     = jit.builder->CreateCall(getTempsFromContext, context);
    Value* const temporary = jit.builder->CreateCall2(getObjectField, temps, jit.builder->getInt32(index));

    std::ostringstream ss;
    ss << "temp" << index << ".";
    temporary->setName(ss.str());

    Value* const holder = protectProducerNode(jit, jit.currentNode, temporary);
    setNodeValue(jit, jit.currentNode, holder);
}

void MethodCompiler::doPushLiteral(TJITContext& jit)
{
    const uint8_t index = jit.currentNode->getInstruction().getArgument();

    TSymbolArray&  literals = *jit.originMethod->literals;
    TObject* const literal  = literals[index];

    Value* result = 0;

    if (isSmallInteger(literal)) {
        Value* const integerValue = jit.builder->getInt32( TInteger(literal).rawValue() );
        result = jit.builder->CreateIntToPtr(integerValue, m_baseTypes.object->getPointerTo());

        std::ostringstream ss;
        ss << "const" << result << ".";
        result->setName(ss.str());
    } else {
        Function* const getLiteralFromContext = m_JITModule->getFunction("getLiteralFromContext");
        Value* const context = jit.getCurrentContext();
        Value* const literalValue = jit.builder->CreateCall2(getLiteralFromContext, context, jit.builder->getInt32(index));

        std::ostringstream ss;
        ss << "lit" << (uint32_t) index << ".";
        literalValue->setName(ss.str());

        result = protectProducerNode(jit, jit.currentNode, literalValue);
    }

    assert(result);
    setNodeValue(jit, jit.currentNode, result);
}

void MethodCompiler::doPushConstant(TJITContext& jit)
{
    const uint32_t constant = jit.currentNode->getInstruction().getArgument();
    Value* constantValue    = 0;

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
            Value* const integerValue = jit.builder->getInt32( TInteger(constant).rawValue() );
            constantValue = jit.builder->CreateIntToPtr(integerValue, m_baseTypes.object->getPointerTo());

            std::ostringstream ss;
            ss << "const" << constant << ".";
            constantValue->setName(ss.str());
        } break;

        case pushConstants::nil:         constantValue = m_globals.nilObject;   break;
        case pushConstants::trueObject:  constantValue = m_globals.trueObject;  break;
        case pushConstants::falseObject: constantValue = m_globals.falseObject; break;

        default:
            std::fprintf(stderr, "JIT: unknown push constant %d\n", constant);
    }

    setNodeValue(jit, jit.currentNode, constantValue);
}

void MethodCompiler::doPushBlock(TJITContext& jit)
{
    st::PushBlockNode* pushBlockNode = jit.currentNode->cast<st::PushBlockNode>();
    st::ParsedBlock*   parsedBlock   = pushBlockNode->getParsedBlock();

    TJITBlockContext blockContext(this, jit.parsedMethod, parsedBlock);

    // Creating block function named Class>>method@offset
    const uint16_t blockOffset = parsedBlock->getStartOffset();
    std::ostringstream ss;
    ss << jit.originMethod->klass->name->toString() + ">>" + jit.originMethod->name->toString() << "@" << blockOffset;
    std::string blockFunctionName = ss.str();

    outs() << "Compiling block " << blockFunctionName << "\n";

//     {
//         std::ostringstream ss;
//         ss << "dots/" << blockFunctionName << ".dot";
//         ControlGraphVisualizer vis(blockContext.controlGraph, ss.str());
//         vis.run();
//     }

    std::vector<Type*> blockParams;
    blockParams.push_back(m_baseTypes.block->getPointerTo()); // block object with context information

    FunctionType* blockFunctionType = FunctionType::get(
        m_baseTypes.object->getPointerTo(), // block return value
        blockParams,               // parameters
        false                      // we're not dealing with vararg
    );

    blockContext.function = m_JITModule->getFunction(blockFunctionName);
    if (! blockContext.function) { // Checking if not already created
        blockContext.function = cast<Function>(m_JITModule->getOrInsertFunction(blockFunctionName, blockFunctionType));

        blockContext.function->setGC("shadow-stack");
        m_blockFunctions[blockFunctionName] = blockContext.function;

        // Creating the basic block and inserting it into the function
        blockContext.preamble = BasicBlock::Create(m_JITModule->getContext(), "blockPreamble", blockContext.function);
        blockContext.builder = new IRBuilder<>(blockContext.preamble);
        writePreamble(blockContext, /*isBlock*/ true);
        scanForBranches(blockContext, blockContext.parsedBlock);

        ss.str("");
        ss << "offset" << blockOffset;
        BasicBlock* const blockBody = parsedBlock->getBasicBlockByOffset(blockOffset)->getValue(); // m_targetToBlockMap[blockOffset];
        assert(blockBody);
        blockBody->setName(ss.str());

        blockContext.builder->CreateBr(blockBody);
        blockContext.builder->SetInsertPoint(blockBody);

        writeFunctionBody(blockContext);

//         outs() << *blockContext.function << "\n";

        // Running optimization passes on a block function
        JITRuntime::Instance()->optimizeFunction(blockContext.function);

//         outs() << *blockContext.function << "\n";
    }

    outs() << "Done compiling block " << blockFunctionName << "\n";

    // Create block object and fill it with context information
    Value* args[] = {
        jit.getCurrentContext(),                   // creatingContext
        jit.builder->getInt8(jit.currentNode->getInstruction().getArgument()), // arg offset
        jit.builder->getInt16(blockOffset)
    };
    Value* blockObject = jit.builder->CreateCall(m_runtimeAPI.createBlock, args);
    blockObject = jit.builder->CreateBitCast(blockObject, m_baseTypes.object->getPointerTo());
    blockObject->setName("block.");

    Value* blockHolder = protectProducerNode(jit, jit.currentNode, blockObject);
    setNodeValue(jit, jit.currentNode, blockHolder);
//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, blockHolder));
}

void MethodCompiler::doAssignTemporary(TJITContext& jit)
{
    const uint8_t index = jit.currentNode->getInstruction().getArgument();
    Value* const  value = getArgument(jit); //jit.lastValue();
    IRBuilder<>& builder = * jit.builder;

    Function* getTempsFromContext = m_JITModule->getFunction("getTempsFromContext");
    Value* const context = jit.getCurrentContext();
    Value* const temps   = builder.CreateCall(getTempsFromContext, context);
    builder.CreateCall3(m_baseFunctions.setObjectField, temps, builder.getInt32(index), value);
}

void MethodCompiler::doAssignInstance(TJITContext& jit)
{
    const uint8_t index = jit.currentNode->getInstruction().getArgument();
    Value* const  value = getArgument(jit); // jit.lastValue();
    IRBuilder<>& builder = * jit.builder;

    Function* const getObjectFieldPtr = m_JITModule->getFunction("getObjectFieldPtr");
    Value*    const self = jit.getSelf();
    Value*    const fieldPointer = builder.CreateCall2(getObjectFieldPtr, self, builder.getInt32(index));

    builder.CreateCall2(m_runtimeAPI.checkRoot, value, fieldPointer);
    builder.CreateStore(value, fieldPointer);
}

void MethodCompiler::doMarkArguments(TJITContext& jit)
{
    // Here we need to create the arguments array from the values on the stack
    const uint8_t argumentsCount = jit.currentNode->getInstruction().getArgument();

    // FIXME Probably we may unroll the arguments array and pass the values directly.
    //       However, in some cases this may lead to additional architectural problems.
    Value* const argumentsObject = createArray(jit, argumentsCount);

    // Filling object with contents
    uint8_t index = argumentsCount;
    assert(argumentsCount == jit.currentNode->getArgumentsCount());
    while (index > 0) {
        Value* const argument = getArgument(jit, index - 1); // jit.popValue();
        jit.builder->CreateCall3(m_baseFunctions.setObjectField, argumentsObject, jit.builder->getInt32(--index), argument);
    }

    Value* const argumentsArray = jit.builder->CreateBitCast(argumentsObject, m_baseTypes.objectArray->getPointerTo());
    Value* const argsHolder = protectProducerNode(jit, jit.currentNode, argumentsArray);
    argsHolder->setName("pArgs.");

    setNodeValue(jit, jit.currentNode, argsHolder);
    //     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, argsHolder));
}

void MethodCompiler::doSendUnary(TJITContext& jit)
{
    Value* const value = getArgument(jit); // jit.popValue();

    Value* condition = 0;
    switch ( static_cast<unaryBuiltIns::Opcode>(jit.currentNode->getInstruction().getArgument()) ) {
        case unaryBuiltIns::isNil:  condition = jit.builder->CreateICmpEQ(value, m_globals.nilObject, "isNil.");  break;
        case unaryBuiltIns::notNil: condition = jit.builder->CreateICmpNE(value, m_globals.nilObject, "notNil."); break;

        default:
            std::fprintf(stderr, "JIT: Invalid opcode %d passed to sendUnary\n", jit.currentNode->getInstruction().getArgument());
    }

    // FIXME Do not protect the result object because it will always be the literal value
    Value* const result = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
    setNodeValue(jit, jit.currentNode, result);
    //jit.pushValue(result);
}

llvm::Value* MethodCompiler::getArgument(TJITContext& jit, std::size_t index/* = 0*/) {
    return getNodeValue(jit, jit.currentNode->getArgument(index));
}

Value* MethodCompiler::getNodeValue(TJITContext& jit, st::ControlNode* node, llvm::BasicBlock* insertBlock /*= 0*/)
{
    Value* value = node->getValue();
    if (value) {
        // If value is a holder, loading and returning the stored value.
        if (isa<AllocaInst>(value)) {
            // In phi mode we should insert the load in the incoming block
            if (insertBlock) {
                TerminatorInst* const terminator = insertBlock->getTerminator();
                if (terminator)
                    jit.builder->SetInsertPoint(insertBlock, terminator); // inserting before terminator
                else
                    jit.builder->SetInsertPoint(insertBlock); // appending to the end of the block
            }

            // inserting in the current block
            return jit.builder->CreateLoad(value);
        }

        // Otherwise, returning as is.
        return value;
    }

    if (st::PhiNode* const phiNode = node->cast<st::PhiNode>()) {
        if (!insertBlock) {
            // Storing original insert position to return to
            BasicBlock* const currentBlock = jit.builder->GetInsertBlock();
            BasicBlock::iterator storedInsertPoint = jit.builder->GetInsertPoint();

            // Endoing phi and it's holder
            jit.builder->SetInsertPoint(currentBlock, currentBlock->getFirstInsertionPt());
            PHINode* const phiValue  = jit.builder->CreatePHI(m_baseTypes.object->getPointerTo(), phiNode->getIncomingList().size(), "phi.");
            Value*   const phiHolder = protectProducerNode(jit, phiNode, phiValue);

            phiNode->setPhiValue(phiValue);

            // Appending phi to the post processing list that will fill the incomings
            jit.pendingPhiNodes.push_back(phiNode);

            // Restoring original insert position
            jit.builder->SetInsertPoint(currentBlock, storedInsertPoint);

            // Encoding the value load to be used in requesting instruction
            value = (phiHolder == phiValue) ? static_cast<Value*>(phiValue) : jit.builder->CreateLoad(phiHolder);
        } else {
            Value* const phiHolder = getPhiValue(jit, phiNode);

            jit.builder->SetInsertPoint(insertBlock, insertBlock->getTerminator());
            value = isa<AllocaInst>(phiHolder) ? jit.builder->CreateLoad(phiHolder) : phiHolder;
        }
    }

    assert(value);
    node->setValue(value);

    return value;
}

llvm::Value* MethodCompiler::getPhiValue(TJITContext& jit, st::PhiNode* phiNode)
{
    // Switching to the phi insertion point
    BasicBlock* const phiInsertBlock = phiNode->getDomain()->getBasicBlock()->getValue();
    assert(phiInsertBlock);
    jit.builder->SetInsertPoint(phiInsertBlock, phiInsertBlock->getFirstInsertionPt());

    PHINode* const phiValue = jit.builder->CreatePHI(m_baseTypes.object->getPointerTo(), phiNode->getIncomingList().size(), "phi.");
    phiNode->setPhiValue(phiValue);
    encodePhiIncomings(jit, phiNode);

    // Phi is created at the top of the basic block but consumed later.
    // Value will be broken if GC will be invoked between phi and consumer.
    // Resetting the insertion point which may be changed in getNodeValue() before.
    jit.builder->SetInsertPoint(phiInsertBlock, phiInsertBlock->getFirstInsertionPt());

    return protectProducerNode(jit, phiNode, phiValue);
}

void MethodCompiler::encodePhiIncomings(TJITContext& jit, st::PhiNode* phiNode)
{
    const st::PhiNode::TIncomingList& incomingList = phiNode->getIncomingList();
    for (std::size_t index = 0; index < incomingList.size(); index++) {
        const st::PhiNode::TIncoming& incoming = incomingList[index];

        BasicBlock* const incomingBlock = incoming.domain->getBasicBlock()->getEndValue(); // m_targetToBlockMap[incoming.domain->getBasicBlock()->getOffset()];
        assert(incomingBlock);

        // This call may change the insertion point if one of the incoming values is a value holder,
        // not just a simple value. Load should be inserted in the incoming basic block.
        phiNode->getPhiValue()->addIncoming(getNodeValue(jit, incoming.node, incomingBlock), incomingBlock);
    }
}

void MethodCompiler::setNodeValue(TJITContext& jit, st::ControlNode* node, llvm::Value* value)
{
    assert(value);
    assert(! node->getValue());

    node->setValue(value);
}

void MethodCompiler::doSendBinary(TJITContext& jit)
{
    // 0, 1 or 2 for '<', '<=' or '+' respectively
    binaryBuiltIns::Operator opcode = static_cast<binaryBuiltIns::Operator>(jit.currentNode->getInstruction().getArgument());

    Value* const rightValue = getArgument(jit, 1); // jit.popValue();
    Value* const leftValue  = getArgument(jit, 0); // jit.popValue();

    // Checking if values are both small integers
    Value* const rightIsInt  = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, rightValue);
    Value* const leftIsInt   = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, leftValue);
    Value* const isSmallInts = jit.builder->CreateAnd(rightIsInt, leftIsInt);

    BasicBlock* const integersBlock   = BasicBlock::Create(m_JITModule->getContext(), "asIntegers.", jit.function);
    BasicBlock* const sendBinaryBlock = BasicBlock::Create(m_JITModule->getContext(), "asObjects.",  jit.function);
    BasicBlock* const resultBlock     = BasicBlock::Create(m_JITModule->getContext(), "result.",     jit.function);

//     jit.currentNode->getDomain()->getBasicBlock()->setEndValue(resultBlock);

    // Depending on the contents we may either do the integer operations
    // directly or create a send message call using operand objects
    jit.builder->CreateCondBr(isSmallInts, integersBlock, sendBinaryBlock);

    // Now the integers part
    jit.builder->SetInsertPoint(integersBlock);
    Value* const rightInt = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, rightValue);
    Value* const leftInt  = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, leftValue);

    Value* intResult       = 0; // this will be an immediate operation result
    Value* intResultObject = 0; // this will be actual object to return
    switch (opcode) {
        case binaryBuiltIns::operatorLess:     intResult = jit.builder->CreateICmpSLT(leftInt, rightInt); break;
        case binaryBuiltIns::operatorLessOrEq: intResult = jit.builder->CreateICmpSLE(leftInt, rightInt); break;
        case binaryBuiltIns::operatorPlus:     intResult = jit.builder->CreateAdd(leftInt, rightInt);     break;
        default:
            std::fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", opcode);
    }

    // Checking which operation was performed and
    // processing the intResult object in the proper way
    if (opcode == binaryBuiltIns::operatorPlus) {
        // Result of + operation will be number.
        // We need to create TInteger value and cast it to the pointer

        // Interpreting raw integer value as a pointer
        Value* const smalltalkInt = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult, "intAsPtr.");
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

    // Now creating the argument array
    Value* argumentsObject  = createArray(jit, 2);

    jit.builder->CreateCall3(m_baseFunctions.setObjectField, argumentsObject, jit.builder->getInt32(0), leftValue);
    jit.builder->CreateCall3(m_baseFunctions.setObjectField, argumentsObject, jit.builder->getInt32(1), rightValue);

    Value* argumentsArray    = jit.builder->CreateBitCast(argumentsObject, m_baseTypes.objectArray->getPointerTo());
    Value* sendMessageArgs[] = {
        jit.getCurrentContext(), // calling context
        m_globals.binarySelectors[jit.currentNode->getInstruction().getArgument()],
        argumentsArray,

        // default receiver class
        ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()), //inttoptr 0 works fine too
        jit.builder->getInt32(jit.currentNode->getIndex()) // call site index
    };
    m_callSiteIndexToOffset[m_callSiteIndex++] = jit.currentNode->getIndex();

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
    PHINode* const phi = jit.builder->CreatePHI(m_baseTypes.object->getPointerTo(), 2, "phi.");
    phi->addIncoming(intResultObject, integersBlock);
    phi->addIncoming(sendMessageResult, sendBinaryBlock);

    Value* const resultHolder = protectProducerNode(jit, jit.currentNode, phi);
    setNodeValue(jit, jit.currentNode, resultHolder);
    //jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
}


bool MethodCompiler::doSendMessageToLiteral(TJITContext& jit, st::InstructionNode* receiverNode)
{
    // Optimized version of doSendMessage which takes into account that
    // pending message should be sent to the literal receiver
    // (either constant or a member of method literals). Literal receivers
    // are encoded at the time of method compilation, so thier value and
    // their class will not change over time. Moreover, actual values
    // are known at compile time and may be used to lookup the actual
    // method that should be invoked.

    // Locating message selector
    TSymbolArray& literals   = *jit.originMethod->literals;
    TSymbol* const messageSelector = literals[jit.currentNode->getInstruction().getArgument()];

    TObject* literalReceiver = 0;

    // Determining receiver class
    const st::TSmalltalkInstruction::TOpcode opcode = receiverNode->getInstruction().getOpcode();
    if (opcode == opcode::pushLiteral) {
        literalReceiver = literals[receiverNode->getInstruction().getArgument()];
    } else if (opcode == opcode::pushConstant) {
        const uint8_t constant = receiverNode->getInstruction().getArgument();
        switch(constant) {
            case 0: case 1: case 2: case 3: case 4:
            case 5: case 6: case 7: case 8: case 9:
                literalReceiver = TInteger(constant);
                break;

            case pushConstants::nil:         literalReceiver = globals.nilObject;   break;
            case pushConstants::trueObject:  literalReceiver = globals.trueObject;  break;
            case pushConstants::falseObject: literalReceiver = globals.falseObject; break;
        }
    }

    assert(literalReceiver);
    TClass* const receiverClass = isSmallInteger(literalReceiver) ? globals.smallIntClass : literalReceiver->getClass();

    // Locating a method suitable for a direct call
    TMethod* const directMethod = m_runtime.getVM()->lookupMethod(messageSelector, receiverClass);

    if (! directMethod) {
        outs() << "Error! Could not lookup method for class "  << receiverClass->name->toString() << ", selector " << messageSelector->toString() << "\n";
        return false;
    }

    std::string directFunctionName = directMethod->klass->name->toString() + ">>" + messageSelector->toString();
    Function* directFunction = m_JITModule->getFunction(directFunctionName);

    if (!directFunction) {
        // Compiling function and storing it to the table for further use
        directFunction = compileMethod(directMethod);

        verifyFunction(*directFunction , llvm::AbortProcessAction);

        m_runtime.optimizeFunction(directFunction);
    }

    // Allocating context object and temporaries on the methodFunction's stack.
    // This operation does not affect garbage collector, so no pointer protection
    // is required. Moreover, this is operation is much faster than heap allocation.
    const bool hasTemporaries  = directMethod->temporarySize > 0;
    const uint32_t contextSize = sizeof(TContext);
    const uint32_t tempsSize   = hasTemporaries ? sizeof(TObjectArray) + sizeof(TObject*) * directMethod->temporarySize : 0;

    // Allocating stack space for objects and registering GC protection holder

    MethodCompiler::TStackObject contextPair = allocateStackObject(*jit.builder, sizeof(TContext), 0);
    Value* contextSlot   = contextPair.objectSlot;
    Value* tempsSlot = 0;

    if (hasTemporaries) {
        MethodCompiler::TStackObject tempsPair = allocateStackObject(*jit.builder, sizeof(TObjectArray), directMethod->temporarySize);
        tempsSlot = tempsPair.objectSlot;
    }

    // Filling stack space with zeroes
    jit.builder->CreateMemSet(
        contextSlot,             // destination address
        jit.builder->getInt8(0), // fill with zeroes
        contextSize,             // size of object slot
        0,                       // no alignment
        false                    // volatile operation
    );

    if (hasTemporaries)
        jit.builder->CreateMemSet(
            tempsSlot,                // destination address
            jit.builder->getInt8(0),  // fill with zeroes
            tempsSize,                // size of object slot
            0,                        // no alignment
            false                     // volatile operation
        );

    // Initializing object fields
    // TODO Move the init sequence out of the block or check that it is correctly optimized in loops
    Value* newContextObject  = jit.builder->CreateBitCast(contextSlot, m_baseTypes.object->getPointerTo(), "newContext.");
    Value* newTempsObject    = hasTemporaries ? jit.builder->CreateBitCast(tempsSlot, m_baseTypes.object->getPointerTo(), "newTemps.") : 0;
    Function* setObjectSize  = getBaseFunctions().setObjectSize;
    Function* setObjectClass = getBaseFunctions().setObjectClass;

    // Object size stored in the TSize field of any ordinary object contains
    // number of pointers except for the first two fields
    const uint32_t contextFieldsCount = contextSize / sizeof(TObject*) - 2;

    jit.builder->CreateCall2(setObjectSize, newContextObject, jit.builder->getInt32(contextFieldsCount));
    jit.builder->CreateCall2(setObjectClass, newContextObject, getJitGlobals().contextClass);

    if (hasTemporaries) {
        const uint32_t tempsFieldsCount = tempsSize / sizeof(TObject*) - 2;
        jit.builder->CreateCall2(setObjectSize, newTempsObject, jit.builder->getInt32(tempsFieldsCount));
        jit.builder->CreateCall2(setObjectClass, newTempsObject, getJitGlobals().arrayClass);
    }

    Function* setObjectField  = getBaseFunctions().setObjectField;
    Value* methodRawPointer   = jit.builder->getInt32(reinterpret_cast<uint32_t>(directMethod));
    Value* directMethodObject = jit.builder->CreateIntToPtr(methodRawPointer, m_baseTypes.object->getPointerTo());

    Value* previousContext = jit.getCurrentContext(); // jit.builder->CreateLoad(info.contextHolder);
    Value* contextObject   = jit.builder->CreateBitCast(previousContext, m_baseTypes.object->getPointerTo());

    Value* const arguments = getArgument(jit);
    Value* messageArgumentsObject = jit.builder->CreateBitCast(arguments, m_baseTypes.object->getPointerTo());

    jit.builder->CreateCall3(setObjectField, newContextObject, jit.builder->getInt32(0), directMethodObject);
    jit.builder->CreateCall3(setObjectField, newContextObject, jit.builder->getInt32(1), messageArgumentsObject);
    if (hasTemporaries)
        jit.builder->CreateCall3(setObjectField, newContextObject, jit.builder->getInt32(2), newTempsObject);
    else
        jit.builder->CreateCall3(setObjectField, newContextObject, jit.builder->getInt32(2), getJitGlobals().nilObject);
    jit.builder->CreateCall3(setObjectField, newContextObject, jit.builder->getInt32(3), contextObject);

    Value* newContext = jit.builder->CreateBitCast(newContextObject, m_baseTypes.context->getPointerTo());
    Value* result = 0;

    if (jit.methodHasBlockReturn) {
        // Creating basic block that will be branched to on normal invoke
        BasicBlock* const nextBlock = BasicBlock::Create(m_JITModule->getContext(), "next.", jit.function);
        jit.currentNode->getDomain()->getBasicBlock()->setEndValue(nextBlock);

        // Performing a function invoke
        result = jit.builder->CreateInvoke(directFunction, nextBlock, jit.exceptionLandingPad, newContext);

        // Switching builder to a new block
        jit.builder->SetInsertPoint(nextBlock);
    } else {
        // Just calling the function. No block switching is required
        result = jit.builder->CreateCall(directFunction, newContext);
    }

    Value* const resultHolder = protectProducerNode(jit, jit.currentNode, result);
    setNodeValue(jit, jit.currentNode, resultHolder);

    return true;
}

void MethodCompiler::doSendMessage(TJITContext& jit)
{

    st::InstructionNode* const markArgumentsNode = jit.currentNode->getArgument()->cast<st::InstructionNode>();
    assert(markArgumentsNode);

    st::InstructionNode* const receiverNode = markArgumentsNode->getArgument()->cast<st::InstructionNode>();
    assert(receiverNode);

    // In case of a literal receiver we may encode direct method call
    if (receiverNode->getInstruction().getOpcode() == opcode::pushLiteral ||
        receiverNode->getInstruction().getOpcode() == opcode::pushConstant)
    {
        if (doSendMessageToLiteral(jit, receiverNode))
            return;
    }

    Value* const arguments = getArgument(jit); // jit.popValue();

    // First of all we need to get the actual message selector
    Value* const selectorObject  = jit.getLiteral(jit.currentNode->getInstruction().getArgument());
    Value* const messageSelector = jit.builder->CreateBitCast(selectorObject, m_baseTypes.symbol->getPointerTo());

    std::ostringstream ss;
    ss << "#" << jit.originMethod->literals->getField(jit.currentNode->getInstruction().getArgument())->toString() << ".";
    messageSelector->setName(ss.str());

    // Forming a message parameters
    Value* const sendMessageArgs[] = {
        jit.getCurrentContext(), // calling context
        messageSelector,         // selector
        arguments,               // message arguments

        // default receiver class
        ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()),
        jit.builder->getInt32(jit.currentNode->getIndex()) // call site index
    };
    m_callSiteIndexToOffset[m_callSiteIndex++] = jit.currentNode->getIndex();

    Value* result = 0;
    if (jit.methodHasBlockReturn) {
        // Creating basic block that will be branched to on normal invoke
        BasicBlock* const nextBlock = BasicBlock::Create(m_JITModule->getContext(), "next.", jit.function);
        jit.currentNode->getDomain()->getBasicBlock()->setEndValue(nextBlock);

        // Performing a function invoke
        result = jit.builder->CreateInvoke(m_runtimeAPI.sendMessage, nextBlock, jit.exceptionLandingPad, sendMessageArgs);

        // Switching builder to a new block
        jit.builder->SetInsertPoint(nextBlock);
    } else {
        // Just calling the function. No block switching is required
        result = jit.builder->CreateCall(m_runtimeAPI.sendMessage, sendMessageArgs);
    }

    Value* const resultHolder = protectProducerNode(jit, jit.currentNode, result);
    setNodeValue(jit, jit.currentNode, resultHolder);
//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
}

void MethodCompiler::doSpecial(TJITContext& jit)
{
    const uint8_t opcode = jit.currentNode->getInstruction().getArgument();

//     BasicBlock::iterator iPreviousInst = jit.builder->GetInsertPoint();
//     if (iPreviousInst != jit.builder->GetInsertBlock()->begin())
//         --iPreviousInst;

    switch (opcode) {
        case special::selfReturn:
//             if (! iPreviousInst->isTerminator())
            jit.builder->CreateRet(jit.getSelf());
            break;

        case special::stackReturn:
//             if ( !iPreviousInst->isTerminator() && jit.hasValue() )
            jit.builder->CreateRet(getArgument(jit)); // jit.popValue());
            break;

        case special::blockReturn:
            /*if ( !iPreviousInst->isTerminator() && jit.hasValue())*/ {
                // Peeking the return value from the stack
                Value* const value = getArgument(jit); // jit.popValue();

                // Loading the target context information
                Value* const blockContext = jit.builder->CreateBitCast(jit.getCurrentContext(), m_baseTypes.block->getPointerTo());
                Value* const creatingContextPtr = jit.builder->CreateStructGEP(blockContext, 2);
                Value* const targetContext      = jit.builder->CreateLoad(creatingContextPtr);

                // Emitting the TBlockReturn exception
                jit.builder->CreateCall2(m_runtimeAPI.emitBlockReturn, value, targetContext);

                // This will never be called
                jit.builder->CreateUnreachable();
            }
            break;

        case special::duplicate: {
            // Duplicating the origin value in the dup node.
            // When dup consumers will be remapped to the real
            // value in the ControlGraph this will be redundant.

            Value* const original = getNodeValue(jit, jit.currentNode->getArgument());
            Value* const copy     = protectProducerNode(jit, jit.currentNode, original);

            setNodeValue(jit, jit.currentNode, copy);
            break;
        }

        case special::popTop:
            // Simply doing nothing
            break;

        case special::branch: {
            // Loading branch target bytecode offset
            const uint32_t targetOffset = jit.currentNode->getInstruction().getExtra();

            // Finding appropriate branch target
            // from the previously stored basic blocks
            BasicBlock* const target = jit.controlGraph->getParsedBytecode()->getBasicBlockByOffset(targetOffset)->getValue(); // m_targetToBlockMap[targetOffset];
            assert(target);

            jit.builder->CreateBr(target);
        } break;

        case special::branchIfTrue:
        case special::branchIfFalse: {
            // Loading branch target bytecode offset
            const uint32_t targetOffset = jit.currentNode->getInstruction().getExtra();
            const uint32_t skipOffset   = getSkipOffset(jit.currentNode);

            // Finding appropriate branch target
            // from the previously stored basic blocks
            BasicBlock* const targetBlock = jit.controlGraph->getParsedBytecode()->getBasicBlockByOffset(targetOffset)->getValue(); // m_targetToBlockMap[targetOffset];

            // This is a block that goes right after the branch instruction.
            // If branch condition is not met execution continues right after
            BasicBlock* const skipBlock = jit.controlGraph->getParsedBytecode()->getBasicBlockByOffset(skipOffset)->getValue(); // m_targetToBlockMap[skipOffset];

            // Creating condition check
            Value* const boolObject = (opcode == special::branchIfTrue) ? m_globals.trueObject : m_globals.falseObject;
            Value* const condition  = getArgument(jit); // jit.popValue();
            Value* const boolValue  = jit.builder->CreateICmpEQ(condition, boolObject);
            jit.builder->CreateCondBr(boolValue, targetBlock, skipBlock);

        } break;

        case special::sendToSuper: {
            Value* const argsObject        = getArgument(jit); // jit.popValue();
            Value* const arguments         = jit.builder->CreateBitCast(argsObject, m_baseTypes.objectArray->getPointerTo());

            const uint32_t literalIndex    = jit.currentNode->getInstruction().getExtra();
            Value* const   selectorObject  = jit.getLiteral(literalIndex);
            Value* const   messageSelector = jit.builder->CreateBitCast(selectorObject, m_baseTypes.symbol->getPointerTo());

            Value* const currentClass      = jit.getMethodClass();
            Value* const parentClassPtr    = jit.builder->CreateStructGEP(currentClass, 2);
            Value* const parentClass       = jit.builder->CreateLoad(parentClassPtr);

            Value* const sendMessageArgs[] = {
                jit.getCurrentContext(),     // calling context
                messageSelector, // selector
                arguments,       // message arguments
                parentClass,     // receiver class
                jit.builder->getInt32(jit.currentNode->getIndex()) // call site index
            };
            m_callSiteIndexToOffset[m_callSiteIndex++] = jit.currentNode->getIndex();

            Value* const result = jit.builder->CreateCall(m_runtimeAPI.sendMessage, sendMessageArgs);
            Value* const resultHolder = protectProducerNode(jit, jit.currentNode, result);
            setNodeValue(jit, jit.currentNode, resultHolder);
//             jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
        } break;

        default:
            std::printf("JIT: unknown special opcode %d\n", opcode);
    }
}

uint16_t MethodCompiler::getSkipOffset(st::InstructionNode* branch)
{
    assert(branch->getInstruction().isBranch());
    assert(branch->getInstruction().opcode != special::branch);
    assert(branch->getOutEdges().size() == 2);

    // One of the offsets we know. It is the target offset when condition is met.
    const uint16_t targetOffset = branch->getInstruction().getExtra();

    // Other one will be offset of the block to which points the other edge
    st::TNodeSet::iterator iNode = branch->getOutEdges().begin();
    const st::ControlNode* const edge1 = *iNode++;
    const st::ControlNode* const edge2 = *iNode;

    const uint16_t offset = edge1->getDomain()->getBasicBlock()->getOffset();
    if (offset == targetOffset)
        return edge2->getDomain()->getBasicBlock()->getOffset();
    else
        return offset;
}

void MethodCompiler::doPrimitive(TJITContext& jit)
{
    uint32_t opcode = jit.currentNode->getInstruction().getExtra();

    Value* primitiveResult = 0;
    Value* primitiveFailed = jit.builder->getFalse();
    // br primitiveFailed, primitiveFailedBB, primitiveSucceededBB
    // primitiveSucceededBB:
    //   ret %TObject* primitiveResult
    // primitiveFailedBB:
    //  ;fallback
    //
    // By default we use primitiveFailed BB as a block that collects trash code.
    // llvm passes may eliminate primitiveFailed BB, because "br true, A, B -> br label A" if there are no other paths to B
    // But sometimes CFG of primitive may depend on primitiveFailed result (bulkReplace)
    // If your primitive may fail, you may use 2 ways:
    // 1) set br primitiveFailedBB
    // 2) bind primitiveFailed with any i1 result
    BasicBlock* const primitiveSucceededBB = BasicBlock::Create(m_JITModule->getContext(), "primitiveSucceededBB", jit.function);
    BasicBlock* const primitiveFailedBB = BasicBlock::Create(m_JITModule->getContext(), "primitiveFailedBB", jit.function);
    // FIXME setEndValue() ?

    compilePrimitive(jit, opcode, primitiveResult, primitiveFailed, primitiveSucceededBB, primitiveFailedBB);

    jit.builder->CreateCondBr(primitiveFailed, primitiveFailedBB, primitiveSucceededBB);
    jit.builder->SetInsertPoint(primitiveSucceededBB);

    jit.builder->CreateRet(primitiveResult);
    jit.builder->SetInsertPoint(primitiveFailedBB);

    // FIXME Are we really allowed to use the value without holder?
    setNodeValue(jit, jit.currentNode, m_globals.nilObject);
//     jit.currentNode->getDomain()->getBasicBlock()->setEndValue(primitiveFailedBB);

//     jit.pushValue(m_globals.nilObject);
}


void MethodCompiler::compilePrimitive(TJITContext& jit,
                                    uint8_t opcode,
                                    Value*& primitiveResult,
                                    Value*& primitiveFailed,
                                    BasicBlock* primitiveSucceededBB,
                                    BasicBlock* primitiveFailedBB)
{
    switch (opcode) {
        case primitive::objectsAreEqual: {
            Value* const object2 = getArgument(jit, 1); // jit.popValue();
            Value* const object1 = getArgument(jit, 0); // jit.popValue();

            Value* const result    = jit.builder->CreateICmpEQ(object1, object2);
            Value* const boolValue = jit.builder->CreateSelect(result, m_globals.trueObject, m_globals.falseObject);

            primitiveResult = boolValue;
        } break;

        // TODO ioGetchar
        case primitive::ioPutChar: {
            Value* const intObject = getArgument(jit); // jit.popValue();
            Value* const intValue  = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, intObject);
            Value* const charValue = jit.builder->CreateTrunc(intValue, jit.builder->getInt8Ty());

            Function* const putcharFunc = cast<Function>(m_JITModule->getOrInsertFunction("putchar", jit.builder->getInt32Ty(), jit.builder->getInt8Ty(), NULL));
            jit.builder->CreateCall(putcharFunc, charValue);

            primitiveResult = m_globals.nilObject;
        } break;

        case primitive::getClass: {
            Value* const object = getArgument(jit); // jit.popValue();
            Value* const klass  = jit.builder->CreateCall(m_baseFunctions.getObjectClass, object, "class");
            primitiveResult = jit.builder->CreateBitCast(klass, m_baseTypes.object->getPointerTo());
        } break;

        case primitive::getSize: {
            Value* const object           = getArgument(jit); // jit.popValue();
            Value* const objectIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, object, "isSmallInt");

            BasicBlock* const asSmallInt = BasicBlock::Create(m_JITModule->getContext(), "asSmallInt", jit.function);
            BasicBlock* const asObject   = BasicBlock::Create(m_JITModule->getContext(), "asObject", jit.function);
            jit.builder->CreateCondBr(objectIsSmallInt, asSmallInt, asObject);

            jit.builder->SetInsertPoint(asSmallInt);
            Value* const result = jit.builder->CreateCall(m_baseFunctions.newInteger, jit.builder->getInt32(0));
            jit.builder->CreateRet(result);

            jit.builder->SetInsertPoint(asObject);
            Value* const size = jit.builder->CreateCall(m_baseFunctions.getObjectSize, object, "size");
            primitiveResult   = jit.builder->CreateCall(m_baseFunctions.newInteger, size);
        } break;

        case primitive::startNewProcess: { // 6
            // /* ticks. unused */    jit.popValue();
            Value*    const processObject  = getArgument(jit, 0); // jit.popValue();
            Value*    const process        = jit.builder->CreateBitCast(processObject, m_baseTypes.process->getPointerTo());

            Function* const executeProcess = m_JITModule->getFunction("executeProcess");
            Value*    const processResult  = jit.builder->CreateCall(executeProcess, process);

            primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, processResult);
        } break;

        case primitive::allocateObject: { // 7
            Value* const sizeObject  = getArgument(jit, 1); // jit.popValue();
            Value* const klassObject = getArgument(jit, 0); // jit.popValue();
            Value* const klass       = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());

            Value* const size        = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, sizeObject, "size.");
            Value* const slotSize    = jit.builder->CreateCall(m_baseFunctions.getSlotSize, size, "slotSize.");
            Value* const newInstance = jit.builder->CreateCall2(m_runtimeAPI.newOrdinaryObject, klass, slotSize, "instance.");

            primitiveResult = newInstance;
        } break;

        case primitive::allocateByteArray: { // 20
            Value* const sizeObject  = getArgument(jit, 1); // jit.popValue();
            Value* const klassObject = getArgument(jit, 0); // jit.popValue();

            Value* const klass       = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());
            Value* const dataSize    = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, sizeObject, "dataSize.");
            Value* const newInstance = jit.builder->CreateCall2(m_runtimeAPI.newBinaryObject, klass, dataSize, "instance.");

            primitiveResult = jit.builder->CreateBitCast(newInstance, m_baseTypes.object->getPointerTo() );
        } break;

        case primitive::cloneByteObject: { // 23
            Value* const klassObject    = getArgument(jit, 1); // jit.popValue();
            Value* const original       = getArgument(jit, 0); // jit.popValue();
            Value* const originalHolder = protectPointer(jit, original);

            Value* const klass    = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());
            Value* const dataSize = jit.builder->CreateCall(m_baseFunctions.getObjectSize, original, "dataSize.");
            Value* const clone    = jit.builder->CreateCall2(m_runtimeAPI.newBinaryObject, klass, dataSize, "clone.");

            Value* const originalObject = jit.builder->CreateBitCast(jit.builder->CreateLoad(originalHolder), m_baseTypes.object->getPointerTo());
            Value* const cloneObject    = jit.builder->CreateBitCast(clone, m_baseTypes.object->getPointerTo());
            Value* const sourceFields   = jit.builder->CreateCall(m_baseFunctions.getObjectFields, originalObject);
            Value* const destFields     = jit.builder->CreateCall(m_baseFunctions.getObjectFields, cloneObject);

            Value* const source       = jit.builder->CreateBitCast(sourceFields, jit.builder->getInt8PtrTy());
            Value* const destination  = jit.builder->CreateBitCast(destFields, jit.builder->getInt8PtrTy());

            // Copying the data
            Value* const copyArgs[] = {
                destination,
                source,
                dataSize,
                jit.builder->getInt32(0), // no alignment
                jit.builder->getFalse()   // not volatile
            };

            Type* const memcpyType[] = {jit.builder->getInt8PtrTy(), jit.builder->getInt8PtrTy(), jit.builder->getInt32Ty() };
            Function* const memcpyIntrinsic = getDeclaration(m_JITModule, Intrinsic::memcpy, memcpyType);

            jit.builder->CreateCall(memcpyIntrinsic, copyArgs);

            primitiveResult = cloneObject;
        } break;

        case primitive::integerNew:
            primitiveResult = getArgument(jit); // jit.popValue(); // TODO long integers
            break;

        case primitive::blockInvoke: { // 8
            Value* const object = getArgument(jit); // jit.popValue();
            Value* const block  = jit.builder->CreateBitCast(object, m_baseTypes.block->getPointerTo());

            const uint32_t argCount = jit.currentNode->getInstruction().getArgument() - 1;

            Value* const blockAsContext = jit.builder->CreateBitCast(block, m_baseTypes.context->getPointerTo());
            Function* const getTempsFromContext = m_JITModule->getFunction("getTempsFromContext");
            Value* const blockTemps = jit.builder->CreateCall(getTempsFromContext, blockAsContext);

            Value* const tempsSize = jit.builder->CreateCall(m_baseFunctions.getObjectSize, blockTemps, "tempsSize.");

            Value* const argumentLocationPtr    = jit.builder->CreateStructGEP(block, 1);
            Value* const argumentLocationField  = jit.builder->CreateLoad(argumentLocationPtr);
            Value* const argumentLocationObject = jit.builder->CreateIntToPtr(argumentLocationField, m_baseTypes.object->getPointerTo());
            Value* const argumentLocation       = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, argumentLocationObject, "argLocation.");

            BasicBlock* const tempsChecked = BasicBlock::Create(m_JITModule->getContext(), "tempsChecked.", jit.function);
            tempsChecked->moveAfter(jit.builder->GetInsertBlock());

            //Checking the passed temps size TODO unroll stack
            Value* const blockAcceptsArgCount = jit.builder->CreateSub(tempsSize, argumentLocation, "blockAcceptsArgCount.");
            Value* const tempSizeOk = jit.builder->CreateICmpSLE(jit.builder->getInt32(argCount), blockAcceptsArgCount, "tempSizeOk.");
            jit.builder->CreateCondBr(tempSizeOk, tempsChecked, primitiveFailedBB);
            jit.builder->SetInsertPoint(tempsChecked);

            // Storing values in the block's wrapping context
            for (uint32_t index = argCount - 1, count = argCount; count > 0; index--, count--)
            {
                // (*blockTemps)[argumentLocation + index] = stack[--ec.stackTop];
                Value* const fieldIndex = jit.builder->CreateAdd(argumentLocation, jit.builder->getInt32(index), "fieldIndex.");
                Value* const argument   = getArgument(jit, index + 1); // jit.popValue();
                argument->setName("argument.");
                jit.builder->CreateCall3(m_baseFunctions.setObjectField, blockTemps, fieldIndex, argument);
            }

            Value* const args[] = { block, jit.getCurrentContext() };
            Value* const result = jit.builder->CreateCall(m_runtimeAPI.invokeBlock, args);

            primitiveResult = result;
        } break;

        case primitive::throwError: { //19
            //19 primitive is very special. It raises exception, no code is reachable
            //after calling cxa_throw. But! Someone may add Smalltalk code after <19>
            //Thats why we have to create unconditional br to 'primitiveFailed'
            //to catch any generated code into that BB
            Value* const contextPtr2Size = jit.builder->CreateTrunc(ConstantExpr::getSizeOf(m_baseTypes.context->getPointerTo()->getPointerTo()), jit.builder->getInt32Ty());
            Value* const expnBuffer      = jit.builder->CreateCall(m_exceptionAPI.cxa_allocate_exception, contextPtr2Size);
            Value* const expnTypedBuffer = jit.builder->CreateBitCast(expnBuffer, m_baseTypes.context->getPointerTo()->getPointerTo());
            jit.builder->CreateStore(jit.getCurrentContext(), expnTypedBuffer);

            Value* const throwArgs[] = {
                expnBuffer,
                jit.builder->CreateBitCast(m_exceptionAPI.contextTypeInfo, jit.builder->getInt8PtrTy()),
                ConstantPointerNull::get(jit.builder->getInt8PtrTy())
            };

            jit.builder->CreateCall(m_exceptionAPI.cxa_throw, throwArgs);
            primitiveResult = m_globals.nilObject;
        } break;

        case primitive::arrayAt:       // 24
        case primitive::arrayAtPut: {  // 5
            std::size_t argIndex = (opcode == primitive::arrayAtPut) ? 2 : 1;
            Value* const indexObject = getArgument(jit, argIndex--); // jit.popValue();
            Value* const arrayObject = getArgument(jit, argIndex--); // jit.popValue();
            Value* const valueObejct = (opcode == primitive::arrayAtPut) ? getArgument(jit, argIndex--) : 0;

            BasicBlock* const indexChecked = BasicBlock::Create(m_JITModule->getContext(), "indexChecked.", jit.function);

            //Checking whether index is Smallint //TODO jump to primitiveFailed if not
            Value* const indexIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, indexObject);

            Value* const index       = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, indexObject);
            Value* const actualIndex = jit.builder->CreateSub(index, jit.builder->getInt32(1));

            //Checking boundaries
            Value* const arraySize   = jit.builder->CreateCall(m_baseFunctions.getObjectSize, arrayObject);
            Value* const indexGEZero = jit.builder->CreateICmpSGE(actualIndex, jit.builder->getInt32(0));
            Value* const indexLTSize = jit.builder->CreateICmpSLT(actualIndex, arraySize);
            Value* const boundaryOk  = jit.builder->CreateAnd(indexGEZero, indexLTSize);

            Value* const indexOk = jit.builder->CreateAnd(indexIsSmallInt, boundaryOk);
            jit.builder->CreateCondBr(indexOk, indexChecked, primitiveFailedBB);
            jit.builder->SetInsertPoint(indexChecked);

            if (opcode == primitive::arrayAtPut) {
                Function* const getObjectFieldPtr = m_JITModule->getFunction("getObjectFieldPtr");
                Value*    const fieldPointer = jit.builder->CreateCall2(getObjectFieldPtr, arrayObject, actualIndex);
                jit.builder->CreateCall2(m_runtimeAPI.checkRoot, valueObejct, fieldPointer);
                jit.builder->CreateStore(valueObejct, fieldPointer);

                primitiveResult = arrayObject;
            } else {
                primitiveResult = jit.builder->CreateCall2(m_baseFunctions.getObjectField, arrayObject, actualIndex);
            }
        } break;

        case primitive::stringAt:       // 21
        case primitive::stringAtPut: {  // 22
            std::size_t argIndex = (opcode == primitive::stringAtPut) ? 2 : 1;
            Value* const indexObject = getArgument(jit, argIndex--); // jit.popValue();
            Value* const stringObject = getArgument(jit, argIndex--); // jit.popValue();
            Value* const valueObejct = (opcode == primitive::stringAtPut) ? getArgument(jit, argIndex--) : 0;

            BasicBlock* const indexChecked = BasicBlock::Create(m_JITModule->getContext(), "indexChecked.", jit.function);
            indexChecked->moveAfter(jit.builder->GetInsertBlock());

            //Checking whether index is Smallint //TODO jump to primitiveFailed if not
            Value* const indexIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, indexObject);

            // Acquiring integer value of the index (from the smalltalk's TInteger)
            Value* const index       = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, indexObject);
            Value* const actualIndex = jit.builder->CreateSub(index, jit.builder->getInt32(1));

            //Checking boundaries
            Value* const stringSize  = jit.builder->CreateCall(m_baseFunctions.getObjectSize, stringObject);
            Value* const indexGEZero = jit.builder->CreateICmpSGE(actualIndex, jit.builder->getInt32(0));
            Value* const indexLTSize = jit.builder->CreateICmpSLT(actualIndex, stringSize);
            Value* const boundaryOk  = jit.builder->CreateAnd(indexGEZero, indexLTSize);

            Value* const indexOk     = jit.builder->CreateAnd(indexIsSmallInt, boundaryOk, "indexOk.");
            jit.builder->CreateCondBr(indexOk, indexChecked, primitiveFailedBB);
            jit.builder->SetInsertPoint(indexChecked);

            // Getting access to the actual indexed byte location
            Value* const fields     = jit.builder->CreateCall(m_baseFunctions.getObjectFields, stringObject);
            Value* const bytes      = jit.builder->CreateBitCast(fields, jit.builder->getInt8PtrTy());
            Value* const bytePtr    = jit.builder->CreateGEP(bytes, actualIndex);

            if (opcode == primitive::stringAtPut) {
                // Popping new value from the stack, getting actual integral value from the TInteger
                // then shrinking it to the 1 byte representation and inserting into the pointed location
                Value* const valueInt = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, valueObejct);
                Value* const byte = jit.builder->CreateTrunc(valueInt, jit.builder->getInt8Ty());
                jit.builder->CreateStore(byte, bytePtr);

                primitiveResult = stringObject;
            } else {
                // Loading string byte pointed by the pointer,
                // expanding it to the 4 byte integer and returning
                // as TInteger value

                Value* const byte = jit.builder->CreateLoad(bytePtr);
                Value* const expandedByte = jit.builder->CreateZExt(byte, jit.builder->getInt32Ty());
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
            Value* const rightObject = getArgument(jit, 1); // jit.popValue();
            Value* const leftObject  = getArgument(jit, 0); // jit.popValue();
            compileSmallIntPrimitive(jit, opcode, leftObject, rightObject, primitiveResult, primitiveFailedBB);
        } break;

        case primitive::bulkReplace: {
            Value* const destination            = getArgument(jit, 4); // jit.popValue();
            Value* const sourceStartOffset      = getArgument(jit, 3); // jit.popValue();
            Value* const source                 = getArgument(jit, 2); // jit.popValue();
            Value* const destinationStopOffset  = getArgument(jit, 1); // jit.popValue();
            Value* const destinationStartOffset = getArgument(jit, 0); // jit.popValue();

            Value* const arguments[]  = {
                destination,
                destinationStartOffset,
                destinationStopOffset,
                source,
                sourceStartOffset
            };

            Value* const isBulkReplaceSucceeded  = jit.builder->CreateCall(m_runtimeAPI.bulkReplace, arguments, "ok.");
            primitiveResult = destination;
            primitiveFailed = jit.builder->CreateNot(isBulkReplaceSucceeded);
        } break;

        case primitive::LLVMsendMessage: {
            Value* const args     = jit.builder->CreateBitCast( getArgument(jit, 1) /*jit.popValue()*/, m_baseTypes.objectArray->getPointerTo() );
            Value* const selector = jit.builder->CreateBitCast( getArgument(jit, 0) /*jit.popValue()*/, m_baseTypes.symbol->getPointerTo() );
            Value* const context  = jit.getCurrentContext();

            Value* const sendMessageArgs[] = {
                context, // calling context
                selector,
                args,
                // default receiver class
                ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()), //inttoptr 0 works fine too
                jit.builder->getInt32(jit.currentNode->getIndex()) // call site index
            };
            m_callSiteIndexToOffset[m_callSiteIndex++] = jit.currentNode->getIndex();

            // Now performing a message call
            Value* sendMessageResult = 0;
            if (jit.methodHasBlockReturn) {
                sendMessageResult = jit.builder->CreateInvoke(m_runtimeAPI.sendMessage, primitiveSucceededBB, jit.exceptionLandingPad, sendMessageArgs);
            } else {
                sendMessageResult = jit.builder->CreateCall(m_runtimeAPI.sendMessage, sendMessageArgs);
            }
            primitiveResult = sendMessageResult;
        } break;

        case primitive::ioGetChar:          // 9
        case primitive::ioFileOpen:         // 100
        case primitive::ioFileClose:        // 103
        case primitive::ioFileSetStatIntoArray:   // 105
        case primitive::ioFileReadIntoByteArray:  // 106
        case primitive::ioFileWriteFromByteArray: // 107
        case primitive::ioFileSeek:         // 108

        case primitive::getSystemTicks:     //253

        default: {
            // Here we need to create the arguments array from the values on the stack
            const uint8_t argumentsCount = jit.currentNode->getInstruction().getArgument();
            Value* const argumentsObject    = createArray(jit, argumentsCount);

            // Filling object with contents
            uint8_t index = argumentsCount;
            while (index > 0) {
                Value* const argument = getArgument(jit); // jit.popValue();
                jit.builder->CreateCall3(m_baseFunctions.setObjectField, argumentsObject, jit.builder->getInt32(--index), argument);
            }

            Value* const argumentsArray = jit.builder->CreateBitCast(argumentsObject, m_baseTypes.objectArray->getPointerTo());
            Value* const primitiveFailedPtr = jit.builder->CreateAlloca(jit.builder->getInt1Ty(), 0, "primitiveFailedPtr");
            jit.builder->CreateStore(jit.builder->getFalse(), primitiveFailedPtr);

            primitiveResult = jit.builder->CreateCall3(m_runtimeAPI.callPrimitive, jit.builder->getInt8(opcode), argumentsArray, primitiveFailedPtr);
            primitiveFailed = jit.builder->CreateLoad(primitiveFailedPtr);
        }
    }
}

void MethodCompiler::compileSmallIntPrimitive(TJITContext& jit,
                                            uint8_t opcode,
                                            Value* leftObject,
                                            Value* rightObject,
                                            Value*& primitiveResult,
                                            BasicBlock* primitiveFailedBB)
{
    Value* const rightIsInt  = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, rightObject);
    Value* const leftIsInt   = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, leftObject);
    Value* const areIntsCond = jit.builder->CreateAnd(rightIsInt, leftIsInt);

    BasicBlock* areIntsBB = BasicBlock::Create(m_JITModule->getContext(), "areInts", jit.function);
    jit.builder->CreateCondBr(areIntsCond, areIntsBB, primitiveFailedBB);

    jit.builder->SetInsertPoint(areIntsBB);
    Value* const rightOperand = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, rightObject);
    Value* const leftOperand  = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, leftObject);

    switch(opcode) {
        case primitive::smallIntAdd: {
            Value* const intResult = jit.builder->CreateAdd(leftOperand, rightOperand);
            //FIXME overflow
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntDiv: {
            Value* const isZero = jit.builder->CreateICmpEQ(rightOperand, jit.builder->getInt32(0));
            BasicBlock*  divBB  = BasicBlock::Create(m_JITModule->getContext(), "div", jit.function);
            jit.builder->CreateCondBr(isZero, primitiveFailedBB, divBB);

            jit.builder->SetInsertPoint(divBB);
            Value* const intResult = jit.builder->CreateSDiv(leftOperand, rightOperand);
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntMod: {
            Value* const isZero = jit.builder->CreateICmpEQ(rightOperand, jit.builder->getInt32(0));
            BasicBlock*  modBB  = BasicBlock::Create(m_JITModule->getContext(), "mod", jit.function);
            jit.builder->CreateCondBr(isZero, primitiveFailedBB, modBB);

            jit.builder->SetInsertPoint(modBB);
            Value* const intResult = jit.builder->CreateSRem(leftOperand, rightOperand);
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntLess: {
            Value* const condition = jit.builder->CreateICmpSLT(leftOperand, rightOperand);
            primitiveResult  = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
        } break;
        case primitive::smallIntEqual: {
            Value* const condition = jit.builder->CreateICmpEQ(leftOperand, rightOperand);
            primitiveResult  = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
        } break;
        case primitive::smallIntMul: {
            Value* const intResult = jit.builder->CreateMul(leftOperand, rightOperand);
            //FIXME overflow
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntSub: {
            Value* const intResult = jit.builder->CreateSub(leftOperand, rightOperand);
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntBitOr: {
            Value* const intResult = jit.builder->CreateOr(leftOperand, rightOperand);
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntBitAnd: {
            Value* const intResult = jit.builder->CreateAnd(leftOperand, rightOperand);
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntBitShift: {
            BasicBlock* shiftRightBB  = BasicBlock::Create(m_JITModule->getContext(), ">>", jit.function);
            BasicBlock* shiftLeftBB   = BasicBlock::Create(m_JITModule->getContext(), "<<", jit.function);
            BasicBlock* shiftResultBB = BasicBlock::Create(m_JITModule->getContext(), "shiftResult", jit.function);

            Value* const rightIsNeg = jit.builder->CreateICmpSLT(rightOperand, jit.builder->getInt32(0));
            jit.builder->CreateCondBr(rightIsNeg, shiftRightBB, shiftLeftBB);

            jit.builder->SetInsertPoint(shiftRightBB);
            Value* const rightOperandNeg  = jit.builder->CreateNeg(rightOperand);
            Value* const shiftRightResult = jit.builder->CreateAShr(leftOperand, rightOperandNeg);
            jit.builder->CreateBr(shiftResultBB);

            jit.builder->SetInsertPoint(shiftLeftBB);
            Value* const shiftLeftResult = jit.builder->CreateShl(leftOperand, rightOperand);
            Value* const shiftLeftFailed = jit.builder->CreateICmpSGT(leftOperand, shiftLeftResult);
            jit.builder->CreateCondBr(shiftLeftFailed, primitiveFailedBB, shiftResultBB);

            jit.builder->SetInsertPoint(shiftResultBB);
            PHINode* const phi = jit.builder->CreatePHI(jit.builder->getInt32Ty(), 2);
            phi->addIncoming(shiftRightResult, shiftRightBB);
            phi->addIncoming(shiftLeftResult, shiftLeftBB);

            primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, phi);
        } break;
    }
}

MethodCompiler::TStackObject MethodCompiler::allocateStackObject(llvm::IRBuilder<>& builder, uint32_t baseSize, uint32_t fieldsCount)
{
    // Storing current edit location
    BasicBlock* const insertBlock = builder.GetInsertBlock();
    BasicBlock::iterator insertPoint = builder.GetInsertPoint();

    // Switching to the preamble
    BasicBlock* const preamble = insertBlock->getParent()->begin();
    builder.SetInsertPoint(preamble, preamble->begin());

    // Allocating the object slot
    const uint32_t  holderSize = baseSize + sizeof(TObject*) * fieldsCount;
    AllocaInst* const objectSlot = builder.CreateAlloca(builder.getInt8Ty(), builder.getInt32(holderSize));
    objectSlot->setAlignment(4);

    // Allocating object holder in the preamble
    AllocaInst* objectHolder = builder.CreateAlloca(m_baseTypes.object->getPointerTo(), 0, "stackHolder.");

    // Initializing holder with null value
//    builder.CreateStore(ConstantPointerNull::get(m_baseTypes.object->getPointerTo()), objectHolder, true);

    Function* gcrootIntrinsic = getDeclaration(m_JITModule, Intrinsic::gcroot);

    //Value* const structData = { ConstantInt::get(builder.getInt1Ty(), 1) };

    // Registering holder in GC and supplying metadata that tells GC to treat this particular root
    // as a pointer to a stack object. Stack objects are not moved by GC. Instead, only their fields
    // and class pointer are updated.
    //Value* const metaData = ConstantStruct::get(m_JITModule->getTypeByName("TGCMetaData"), ConstantInt::get(builder.getInt1Ty(), 1));
    Value* const metaData = m_JITModule->getGlobalVariable("stackObjectMeta");
    Value* const stackRoot = builder.CreateBitCast(objectHolder, builder.getInt8PtrTy()->getPointerTo());
    builder.CreateCall2(gcrootIntrinsic, stackRoot, builder.CreateBitCast(metaData, builder.getInt8PtrTy()));

    // Returning to the original edit location
    builder.SetInsertPoint(insertBlock, insertPoint);

    // Storing the address of stack object to the holder
    Value* const newObject = builder.CreateBitCast(objectSlot, m_baseTypes.object->getPointerTo());
    builder.CreateStore(newObject, objectHolder/*, true*/);

    TStackObject result;
    result.objectHolder = objectHolder;
    result.objectSlot = objectSlot;
    return result;
}
