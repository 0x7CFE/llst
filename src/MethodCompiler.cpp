/*
 *    MethodCompiler.cpp
 *
 *    Implementation of MethodCompiler class which is used to
 *    translate smalltalk bytecodes to LLVM IR code
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.2
 *
 *    LLST is
 *        Copyright (C) 2012-2013 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2013 by Roman Proskuryakov <humbug@deeptown.org>
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
#include <stdarg.h>
#include <llvm/Support/CFG.h>
#include <iostream>
#include <sstream>
#include <opcodes.h>
#include <analysis.h>

using namespace llvm;

Value* TDeferredValue::get()
{
    IRBuilder<>& builder = * m_jit->builder;
    Module* jitModule = JITRuntime::Instance()->getModule();
    Function* getObjectField = jitModule->getFunction("getObjectField");

    switch (m_operation) {
        case loadHolder:
            return builder.CreateLoad(m_argument);

        case loadArgument: {
            Function* getArgFromContext = jitModule->getFunction("getArgFromContext");
            Value* context  = m_jit->getCurrentContext();
            Value* argument = builder.CreateCall2(getArgFromContext, context, builder.getInt32(m_index));

            std::ostringstream ss;
            ss << "arg" << m_index << ".";
            argument->setName(ss.str());

            return argument;
        } break;

        case loadInstance: {
            Value* self  = m_jit->getSelf();
            Value* field = builder.CreateCall2(getObjectField, self, builder.getInt32(m_index));

            std::ostringstream ss;
            ss << "field" << m_index << ".";
            field->setName(ss.str());

            return field;
        } break;

        case loadTemporary: {
            Function* getTempsFromContext = jitModule->getFunction("getTempsFromContext");
            Value* context   = m_jit->getCurrentContext();
            Value* temps     = builder.CreateCall(getTempsFromContext, context);
            Value* temporary = builder.CreateCall2(getObjectField, temps, builder.getInt32(m_index));

            std::ostringstream ss;
            ss << "temp" << m_index << ".";
            temporary->setName(ss.str());

            return temporary;
        } break;

        case loadLiteral: {
            TMethod* method  = m_jit->originMethod;
            TObject* literal = method->literals->getField(m_index);

            Value* literalValue = builder.CreateIntToPtr(
                builder.getInt32( reinterpret_cast<uint32_t>(literal)),
                m_jit->compiler->getBaseTypes().object->getPointerTo()
            );

            std::ostringstream ss;
            ss << "lit" << (uint32_t) m_index << ".";
            literalValue->setName(ss.str());

            return literalValue;
//             return m_jit->getLiteral(m_index);
        } break;

        default:
            outs() << "Unknown deferred operation: " << m_operation << "\n";
            return 0;
    }
}

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
    function->addFnAttr(Attributes(Attribute::InlineHint));
//     function->addFnAttr(Attributes(Attribute::AlwaysInline));
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
    jit.contextHolder = protectPointer(jit, context);
    jit.contextHolder->setName("pContext");

    // Storing self pointer
    Value* pargs     = jit.builder->CreateStructGEP(context, 2);
    Value* arguments = jit.builder->CreateLoad(pargs);
    Value* pobject   = jit.builder->CreateBitCast(arguments, m_baseTypes.object->getPointerTo());
    Value* self      = jit.builder->CreateCall2(m_baseFunctions.getObjectField, pobject, jit.builder->getInt32(0));
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

void MethodCompiler::scanForBranches(TJITContext& jit, uint32_t byteCount /*= 0*/)
{
    // Iterating over method's basic blocks and creating their representation in LLVM
    // Created blocks are collected in the m_targetToBlockMap map with bytecode offset as a key

    class Visitor : public st::BasicBlockVisitor {
    public:
        Visitor(TJITContext& jit) : BasicBlockVisitor(jit.parsedMethod), m_jit(jit) { }

    private:
        virtual bool visitBlock(st::BasicBlock& basicBlock) {
            MethodCompiler* compiler = m_jit.compiler; // self reference
            llvm::BasicBlock* newBlock = llvm::BasicBlock::Create(
                compiler->m_JITModule->getContext(),   // creating context
                "branch.",                             // new block's name
                m_jit.function                         // method's function
            );

            compiler->m_targetToBlockMap[basicBlock.getOffset()] = newBlock;
            return true;
        }

        TJITContext& m_jit;
    };

    Visitor visitor(jit);
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
    scanForBranches(jit);

    // Switching builder context to the first basic block from the preamble
    BasicBlock* body = m_targetToBlockMap[0];
    body->setName("body.");

    jit.builder->SetInsertPoint(jit.preamble);
    jit.builder->CreateBr(body);

    // Resetting the builder to the body
    jit.builder->SetInsertPoint(body);

    // Processing the method's bytecodes
    writeFunctionBody(jit);

    // Cleaning up
    m_blockFunctions.clear();
    m_targetToBlockMap.clear();

    return jit.function;
}

void MethodCompiler::writeFunctionBody(TJITContext& jit)
{
    class Visitor : public st::NodeVisitor {
    public:
        Visitor(TJITContext& jit) : st::NodeVisitor(jit.controlGraph), m_jit(jit) { }

    private:
        virtual bool visitDomain(st::ControlDomain& domain) {
            llvm::BasicBlock* newBlock = m_jit.compiler->m_targetToBlockMap[domain.getBasicBlock()->getOffset()];

            newBlock->moveAfter(m_jit.builder->GetInsertBlock()); // for a pretty sequenced BB output
            m_jit.builder->SetInsertPoint(newBlock);

            return NodeVisitor::visitDomain(domain);
        }

        virtual bool visitNode(st::ControlNode& node) {
            m_jit.currentNode = node.cast<st::InstructionNode>();
            assert(m_jit.currentNode);

            m_jit.compiler->writeInstruction(m_jit);
            return NodeVisitor::visitNode(node);
        }

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
    // Self is interpreted as object array.
    // Array elements are instance variables

    uint8_t index = jit.currentNode->getInstruction().getArgument();

//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadInstance, index));
}

void MethodCompiler::doPushArgument(TJITContext& jit)
{
    uint8_t index = jit.currentNode->getInstruction().getArgument();

    st::TNodeList consumers = jit.currentNode->getConsumers();
//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadArgument, index));
}

void MethodCompiler::doPushTemporary(TJITContext& jit)
{
    uint8_t index = jit.currentNode->getInstruction().getArgument();
//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadTemporary, index));
}

void MethodCompiler::doPushLiteral(TJITContext& jit)
{
    uint8_t index = jit.currentNode->getInstruction().getArgument();
//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadLiteral, index));
}

void MethodCompiler::doPushConstant(TJITContext& jit)
{
    const uint32_t constant = jit.currentNode->getInstruction().getArgument();
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
            Value* integerValue = jit.builder->getInt32( TInteger(constant).rawValue() );
            constantValue       = jit.builder->CreateIntToPtr(integerValue, m_baseTypes.object->getPointerTo());

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

    jit.currentNode->setValue(constantValue);
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
        scanForBranches(blockContext);

        BasicBlock* blockBody = BasicBlock::Create(m_JITModule->getContext(), "blockBody", blockContext.function);
        blockContext.builder->CreateBr(blockBody);
        blockContext.builder->SetInsertPoint(blockBody);

        writeFunctionBody(blockContext);

        // Running optimization passes on a block function
        JITRuntime::Instance()->optimizeFunction(blockContext.function);
    }

    // Create block object and fill it with context information
    Value* args[] = {
        jit.getCurrentContext(),                   // creatingContext
        jit.builder->getInt8(jit.currentNode->getInstruction().getArgument()), // arg offset
        jit.builder->getInt16(blockOffset)
    };
    Value* blockObject = jit.builder->CreateCall(m_runtimeAPI.createBlock, args);
    blockObject = jit.builder->CreateBitCast(blockObject, m_baseTypes.object->getPointerTo());
    blockObject->setName("block.");

    Value* blockHolder = protectPointer(jit, blockObject);
//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, blockHolder));
}

void MethodCompiler::doAssignTemporary(TJITContext& jit)
{
    uint8_t index = jit.currentNode->getInstruction().getArgument();
    Value*  value = jit.currentNode->getArgument()->getValue(); //jit.lastValue();
    IRBuilder<>& builder = * jit.builder;

    Function* getTempsFromContext = m_JITModule->getFunction("getTempsFromContext");
    Value* context = jit.getCurrentContext();
    Value* temps   = builder.CreateCall(getTempsFromContext, context);
    builder.CreateCall3(m_baseFunctions.setObjectField, temps, builder.getInt32(index), value);
}

void MethodCompiler::doAssignInstance(TJITContext& jit)
{
    uint8_t index = jit.currentNode->getInstruction().getArgument();
    Value*  value = jit.currentNode->getArgument()->getValue(); // jit.lastValue();
    IRBuilder<>& builder = * jit.builder;

    Value* self  = jit.getSelf();

    Function* getObjectFieldPtr = m_JITModule->getFunction("getObjectFieldPtr");
    Value* fieldPointer = builder.CreateCall2(getObjectFieldPtr, self, builder.getInt32(index));
    builder.CreateCall2(m_runtimeAPI.checkRoot, value, fieldPointer);
    builder.CreateStore(value, fieldPointer);
}

void MethodCompiler::doMarkArguments(TJITContext& jit)
{
    // Here we need to create the arguments array from the values on the stack
    uint8_t argumentsCount = jit.currentNode->getInstruction().getArgument();

    // FIXME Probably we may unroll the arguments array and pass the values directly.
    //       However, in some cases this may lead to additional architectural problems.
    Value* argumentsObject    = createArray(jit, argumentsCount);

    // Filling object with contents
    uint8_t index = argumentsCount;
    assert(argumentsCount == jit.currentNode->getArgumentsCount());
    while (index > 0) {
        Value* value = getArgument(jit, index - 1); // jit.popValue();
        jit.builder->CreateCall3(m_baseFunctions.setObjectField, argumentsObject, jit.builder->getInt32(--index), value);
    }

    Value* argumentsArray = jit.builder->CreateBitCast(argumentsObject, m_baseTypes.objectArray->getPointerTo());
    Value* argsHolder = protectPointer(jit, argumentsArray);
    argsHolder->setName("pArgs.");
//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, argsHolder));
}

void MethodCompiler::doSendUnary(TJITContext& jit)
{
    Value* value     = getArgument(jit); // jit.popValue();
    Value* condition = 0;

    switch ( static_cast<unaryBuiltIns::Opcode>(jit.currentNode->getInstruction().getArgument()) ) {
        case unaryBuiltIns::isNil:  condition = jit.builder->CreateICmpEQ(value, m_globals.nilObject, "isNil.");  break;
        case unaryBuiltIns::notNil: condition = jit.builder->CreateICmpNE(value, m_globals.nilObject, "notNil."); break;

        default:
            std::fprintf(stderr, "JIT: Invalid opcode %d passed to sendUnary\n", jit.currentNode->getInstruction().getArgument());
    }

    Value* result = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
    setNodeValue(jit.currentNode, result);
    //jit.pushValue(result);
}

llvm::Value* MethodCompiler::processLeafNode(st::InstructionNode* instruction)
{

}

llvm::Value* MethodCompiler::getArgument(TJITContext& jit, std::size_t index/* = 0*/) {
    return getNodeValue(jit, jit.currentNode->getArgument(index));
}

Value* MethodCompiler::getNodeValue(TJITContext& jit, st::ControlNode* node)
{
    Value* value = node->getValue();
    if (value)
        return value;

    const std::size_t inEdgesCount = node->getInEdges().size();
    if (st::InstructionNode* instruction = node->cast<st::InstructionNode>()) {
        // If node is a leaf from the same domain we may encode it locally.
        // If not, creating a dummy value wich will be replaced by real value later.

        if (!inEdgesCount && instruction->getDomain() == jit.currentNode->getDomain())
            value = processLeafNode(instruction);
        else
            value = UndefValue::get(jit.builder->getVoidTy());

    } else if (st::PhiNode* phiNode = node->cast<st::PhiNode>()) {
        BasicBlock* const insertBlock = jit.builder->GetInsertBlock();
        const BasicBlock::iterator storedInsertionPoint = jit.builder->GetInsertPoint();
        const BasicBlock::iterator firstInsertionPoint  = insertBlock->getFirstInsertionPt();
        jit.builder->SetInsertPoint(insertBlock, firstInsertionPoint);

        PHINode* const phiValue = jit.builder->CreatePHI(m_baseTypes.object, inEdgesCount);

        st::TNodeSet::iterator iNode = phiNode->getInEdges().begin();
        for (; iNode != phiNode->getInEdges().end(); ++iNode) {
            st::ControlNode* const inNode = *iNode;
            BasicBlock* const inBlock = m_targetToBlockMap[inNode->getDomain()->getBasicBlock()->getOffset()];
            assert(inBlock);

            phiValue->addIncoming(getNodeValue(jit, inNode), inBlock);
        }

        phiNode->setValue(phiValue);
        jit.builder->SetInsertPoint(insertBlock, storedInsertionPoint);

        value = phiValue;
    }

    assert(value);
    return value;
}

void MethodCompiler::setNodeValue(st::ControlNode* node, llvm::Value* value)
{
    assert(value);

    Value* oldValue = node->getValue();
    if (oldValue) {
        if (isa<UndefValue>(oldValue))
            oldValue->replaceAllUsesWith(value);
        else
            assert(false);
    }

    node->setValue(value);
}

void MethodCompiler::doSendBinary(TJITContext& jit)
{
    // 0, 1 or 2 for '<', '<=' or '+' respectively
    binaryBuiltIns::Operator opcode = static_cast<binaryBuiltIns::Operator>(jit.currentNode->getInstruction().getArgument());

    Value* rightValue = getArgument(jit, 1); // jit.popValue();
    Value* leftValue  = getArgument(jit, 0); // jit.popValue();

    // Checking if values are both small integers
    Value*    rightIsInt  = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, rightValue);
    Value*    leftIsInt   = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, leftValue);
    Value*    isSmallInts = jit.builder->CreateAnd(rightIsInt, leftIsInt);

    BasicBlock* integersBlock   = BasicBlock::Create(m_JITModule->getContext(), "asIntegers.", jit.function);
    BasicBlock* sendBinaryBlock = BasicBlock::Create(m_JITModule->getContext(), "asObjects.",  jit.function);
    BasicBlock* resultBlock     = BasicBlock::Create(m_JITModule->getContext(), "result.",     jit.function);

    // Linking pop-chain within the current logical block
    jit.basicBlockContexts[resultBlock].referers.insert(jit.builder->GetInsertBlock());

    // Depending on the contents we may either do the integer operations
    // directly or create a send message call using operand objects
    jit.builder->CreateCondBr(isSmallInts, integersBlock, sendBinaryBlock);

    // Now the integers part
    jit.builder->SetInsertPoint(integersBlock);
    Value*    rightInt     = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, rightValue);
    Value*    leftInt      = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, leftValue);

    Value* intResult       = 0; // this will be an immediate operation result
    Value* intResultObject = 0; // this will be actual object to return
    switch (opcode) {
        case binaryBuiltIns::operatorLess    : intResult = jit.builder->CreateICmpSLT(leftInt, rightInt); break;
        case binaryBuiltIns::operatorLessOrEq: intResult = jit.builder->CreateICmpSLE(leftInt, rightInt); break;
        case binaryBuiltIns::operatorPlus    : intResult = jit.builder->CreateAdd(leftInt, rightInt);     break;
        default:
            std::fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", opcode);
    }

    // Checking which operation was performed and
    // processing the intResult object in the proper way
    if (opcode == binaryBuiltIns::operatorPlus) {
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

    Value* restoredLeftValue  = jit.builder->CreateLoad(leftValueHolder);
    Value* restoredRightValue = jit.builder->CreateLoad(rightValueHolder);
    jit.builder->CreateCall3(m_baseFunctions.setObjectField, argumentsObject, jit.builder->getInt32(0), restoredLeftValue);
    jit.builder->CreateCall3(m_baseFunctions.setObjectField, argumentsObject, jit.builder->getInt32(1), restoredRightValue);

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
    PHINode* phi = jit.builder->CreatePHI(m_baseTypes.object->getPointerTo(), 2, "phi.");
    phi->addIncoming(intResultObject, integersBlock);
    phi->addIncoming(sendMessageResult, sendBinaryBlock);

    Value* resultHolder = protectPointer(jit, phi);
    setNodeValue(jit.currentNode, resultHolder);
    //jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
}

void MethodCompiler::doSendMessage(TJITContext& jit)
{
    Value* arguments = getArgument(jit); // jit.popValue();

    // First of all we need to get the actual message selector
    Value* selectorObject  = jit.getLiteral(jit.currentNode->getInstruction().getArgument());
    Value* messageSelector = jit.builder->CreateBitCast(selectorObject, m_baseTypes.symbol->getPointerTo());

    std::ostringstream ss;
    ss << "#" << jit.originMethod->literals->getField(jit.currentNode->getInstruction().getArgument())->toString() << ".";
    messageSelector->setName(ss.str());

    // Forming a message parameters
    Value* sendMessageArgs[] = {
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
//     jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
}

void MethodCompiler::doSpecial(TJITContext& jit)
{
    const uint8_t opcode = jit.currentNode->getInstruction().getArgument();

    BasicBlock::iterator iPreviousInst = jit.builder->GetInsertPoint();
    if (iPreviousInst != jit.builder->GetInsertBlock()->begin())
        --iPreviousInst;

    switch (opcode) {
        case special::selfReturn:
            if (! iPreviousInst->isTerminator())
                jit.builder->CreateRet(jit.getSelf());
            break;

        case special::stackReturn:
//             if ( !iPreviousInst->isTerminator() && jit.hasValue() )
                jit.builder->CreateRet(getArgument(jit)); // jit.popValue());
            break;

        case special::blockReturn:
            /*if ( !iPreviousInst->isTerminator() && jit.hasValue())*/ {
                // Peeking the return value from the stack
                Value* value = getArgument(jit); // jit.popValue();

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
            {
                // We're popping the value from the stack to a temporary holder
                // and then pushing two lazy stack values pointing to it.

                Value* dupValue  = getArgument(jit); // jit.popValue();
                Value* dupHolder = protectPointer(jit, dupValue);
                dupHolder->setName("pDup.");

                // Two equal values are pushed on the stack
                jit.currentNode->setValue(dupHolder);

                // jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, dupHolder));
                // jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, dupHolder));
            }

            //jit.pushValue(jit.lastValue());
            break;

        case special::popTop:
            // This should be completely eliminated by graph constructor
            assert(false);
//             if (jit.hasValue())
//                 jit.popValue(0, true);
            break;

        case special::branch: {
            // Loading branch target bytecode offset
            uint32_t targetOffset = jit.currentNode->getInstruction().getExtra();

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
            uint32_t targetOffset = jit.currentNode->getInstruction().getExtra();

            if (!iPreviousInst->isTerminator()) {
                // Finding appropriate branch target
                // from the previously stored basic blocks
                BasicBlock* targetBlock = m_targetToBlockMap[targetOffset];

                // This is a block that goes right after the branch instruction.
                // If branch condition is not met execution continues right after
                BasicBlock* skipBlock = BasicBlock::Create(m_JITModule->getContext(), "branchSkip.", jit.function);

                // Creating condition check
                Value* boolObject = (opcode == special::branchIfTrue) ? m_globals.trueObject : m_globals.falseObject;
                Value* condition  = getArgument(jit); // jit.popValue();
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
            Value* argsObject        = getArgument(jit); // jit.popValue();
            Value* arguments         = jit.builder->CreateBitCast(argsObject, m_baseTypes.objectArray->getPointerTo());

            uint32_t literalIndex    = jit.currentNode->getInstruction().getExtra();
            Value*   selectorObject  = jit.getLiteral(literalIndex);
            Value*   messageSelector = jit.builder->CreateBitCast(selectorObject, m_baseTypes.symbol->getPointerTo());

            Value* currentClass      = jit.getMethodClass();
            Value* parentClassPtr    = jit.builder->CreateStructGEP(currentClass, 2);
            Value* parentClass       = jit.builder->CreateLoad(parentClassPtr);

            Value* sendMessageArgs[] = {
                jit.getCurrentContext(),     // calling context
                messageSelector, // selector
                arguments,       // message arguments
                parentClass,     // receiver class
                jit.builder->getInt32(jit.currentNode->getIndex()) // call site index
            };
            m_callSiteIndexToOffset[m_callSiteIndex++] = jit.currentNode->getIndex();

            Value* result = jit.builder->CreateCall(m_runtimeAPI.sendMessage, sendMessageArgs);
            Value* resultHolder = protectPointer(jit, result);
//             jit.pushValue(new TDeferredValue(&jit, TDeferredValue::loadHolder, resultHolder));
        } break;

        default:
            std::printf("JIT: unknown special opcode %d\n", opcode);
    }
}

void MethodCompiler::doPrimitive(TJITContext& jit)
{
    uint32_t opcode = jit.currentNode->getInstruction().getArgument();

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
    BasicBlock* primitiveSucceededBB = BasicBlock::Create(m_JITModule->getContext(), "primitiveSucceededBB", jit.function);
    BasicBlock* primitiveFailedBB = BasicBlock::Create(m_JITModule->getContext(), "primitiveFailedBB", jit.function);

    // Linking pop chain
    jit.basicBlockContexts[primitiveFailedBB].referers.insert(jit.builder->GetInsertBlock());

    compilePrimitive(jit, opcode, primitiveResult, primitiveFailed, primitiveSucceededBB, primitiveFailedBB);
    jit.currentNode->setValue(primitiveResult);

    // Linking pop chain
    jit.basicBlockContexts[primitiveSucceededBB].referers.insert(jit.builder->GetInsertBlock());

    jit.builder->CreateCondBr(primitiveFailed, primitiveFailedBB, primitiveSucceededBB);
    jit.builder->SetInsertPoint(primitiveSucceededBB);

    jit.builder->CreateRet(primitiveResult);
    jit.builder->SetInsertPoint(primitiveFailedBB);

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
            Value* object2 = getArgument(jit, 1); // jit.popValue();
            Value* object1 = getArgument(jit, 0); // jit.popValue();

            Value* result    = jit.builder->CreateICmpEQ(object1, object2);
            Value* boolValue = jit.builder->CreateSelect(result, m_globals.trueObject, m_globals.falseObject);

            primitiveResult = boolValue;
        } break;

        // TODO ioGetchar
        case primitive::ioPutChar: {
            Value* intObject = getArgument(jit); // jit.popValue();
            Value* intValue  = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, intObject);
            Value* charValue = jit.builder->CreateTrunc(intValue, jit.builder->getInt8Ty());

            Function* putcharFunc = cast<Function>(m_JITModule->getOrInsertFunction("putchar", jit.builder->getInt32Ty(), jit.builder->getInt8Ty(), NULL));
            jit.builder->CreateCall(putcharFunc, charValue);

            primitiveResult = m_globals.nilObject;
        } break;

        case primitive::getClass: {
            Value* object = getArgument(jit); // jit.popValue();
            Value* klass  = jit.builder->CreateCall(m_baseFunctions.getObjectClass, object, "class");
            primitiveResult = jit.builder->CreateBitCast(klass, m_baseTypes.object->getPointerTo());
        } break;
        case primitive::getSize: {
            Value* object           = getArgument(jit); // jit.popValue();
            Value* objectIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, object, "isSmallInt");

            BasicBlock* asSmallInt = BasicBlock::Create(m_JITModule->getContext(), "asSmallInt", jit.function);
            BasicBlock* asObject   = BasicBlock::Create(m_JITModule->getContext(), "asObject", jit.function);
            jit.builder->CreateCondBr(objectIsSmallInt, asSmallInt, asObject);

            jit.builder->SetInsertPoint(asSmallInt);
            Value* result = jit.builder->CreateCall(m_baseFunctions.newInteger, jit.builder->getInt32(0));
            jit.builder->CreateRet(result);

            jit.builder->SetInsertPoint(asObject);
            Value* size     = jit.builder->CreateCall(m_baseFunctions.getObjectSize, object, "size");
            primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, size);
        } break;

        case primitive::startNewProcess: { // 6
            // /* ticks. unused */    jit.popValue();
            Value* processObject = getArgument(jit, 1); // jit.popValue();
            Value* process       = jit.builder->CreateBitCast(processObject, m_baseTypes.process->getPointerTo());

            Function* executeProcess = m_JITModule->getFunction("executeProcess");
            Value*    processResult  = jit.builder->CreateCall(executeProcess, process);

            primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, processResult);
        } break;

        case primitive::allocateObject: { // 7
            Value* sizeObject  = getArgument(jit, 1); // jit.popValue();
            Value* klassObject = getArgument(jit, 0); // jit.popValue();
            Value* klass       = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());

            Value* size        = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, sizeObject, "size.");
            Value* slotSize    = jit.builder->CreateCall(m_baseFunctions.getSlotSize, size, "slotSize.");
            Value* newInstance = jit.builder->CreateCall2(m_runtimeAPI.newOrdinaryObject, klass, slotSize, "instance.");

            primitiveResult = newInstance;
        } break;

        case primitive::allocateByteArray: { // 20
            Value* sizeObject  = getArgument(jit, 1); // jit.popValue();
            Value* klassObject = getArgument(jit, 0); // jit.popValue();

            Value* klass       = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());
            Value* dataSize    = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, sizeObject, "dataSize.");
            Value* newInstance = jit.builder->CreateCall2(m_runtimeAPI.newBinaryObject, klass, dataSize, "instance.");

            primitiveResult = jit.builder->CreateBitCast(newInstance, m_baseTypes.object->getPointerTo() );
        } break;

        case primitive::cloneByteObject: { // 23
            Value* klassObject    = getArgument(jit, 1); // jit.popValue();
            Value* original       = getArgument(jit, 0); // jit.popValue();
            Value* originalHolder = protectPointer(jit, original);

            Value* klass    = jit.builder->CreateBitCast(klassObject, m_baseTypes.klass->getPointerTo());
            Value* dataSize = jit.builder->CreateCall(m_baseFunctions.getObjectSize, original, "dataSize.");
            Value* clone    = jit.builder->CreateCall2(m_runtimeAPI.newBinaryObject, klass, dataSize, "clone.");

            Value* originalObject = jit.builder->CreateBitCast(jit.builder->CreateLoad(originalHolder), m_baseTypes.object->getPointerTo());
            Value* cloneObject    = jit.builder->CreateBitCast(clone, m_baseTypes.object->getPointerTo());
            Value* sourceFields   = jit.builder->CreateCall(m_baseFunctions.getObjectFields, originalObject);
            Value* destFields     = jit.builder->CreateCall(m_baseFunctions.getObjectFields, cloneObject);

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
            primitiveResult = getArgument(jit); // jit.popValue(); // TODO long integers
            break;

        case primitive::blockInvoke: { // 8
            Value* object = getArgument(jit); // jit.popValue();
            Value* block  = jit.builder->CreateBitCast(object, m_baseTypes.block->getPointerTo());

            int32_t argCount = jit.currentNode->getInstruction().getArgument() - 1;

            Value* blockAsContext = jit.builder->CreateBitCast(block, m_baseTypes.context->getPointerTo());
            Function* getTempsFromContext = m_JITModule->getFunction("getTempsFromContext");
            Value* blockTemps = jit.builder->CreateCall(getTempsFromContext, blockAsContext);

            Value* tempsSize = jit.builder->CreateCall(m_baseFunctions.getObjectSize, blockTemps, "tempsSize.");

            Value* argumentLocationPtr    = jit.builder->CreateStructGEP(block, 1);
            Value* argumentLocationField  = jit.builder->CreateLoad(argumentLocationPtr);
            Value* argumentLocationObject = jit.builder->CreateIntToPtr(argumentLocationField, m_baseTypes.object->getPointerTo());
            Value* argumentLocation       = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, argumentLocationObject, "argLocation.");

            BasicBlock* tempsChecked = BasicBlock::Create(m_JITModule->getContext(), "tempsChecked.", jit.function);

            //Checking the passed temps size TODO unroll stack
            Value* blockAcceptsArgCount = jit.builder->CreateSub(tempsSize, argumentLocation);
            Value* tempSizeOk = jit.builder->CreateICmpSLE(jit.builder->getInt32(argCount), blockAcceptsArgCount);
            jit.builder->CreateCondBr(tempSizeOk, tempsChecked, primitiveFailedBB);

            jit.basicBlockContexts[tempsChecked].referers.insert(jit.builder->GetInsertBlock());
            jit.builder->SetInsertPoint(tempsChecked);

            // Storing values in the block's wrapping context
            for (uint32_t index = argCount - 1, count = argCount; count > 0; index--, count--)
            {
                // (*blockTemps)[argumentLocation + index] = stack[--ec.stackTop];
                Value* fieldIndex = jit.builder->CreateAdd(argumentLocation, jit.builder->getInt32(index));
                Value* argument   = getArgument(jit, index); // jit.popValue();
                jit.builder->CreateCall3(m_baseFunctions.setObjectField, blockTemps, fieldIndex, argument);
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
            primitiveResult = m_globals.nilObject;
        } break;

        case primitive::arrayAt:       // 24
        case primitive::arrayAtPut: {  // 5
            std::size_t argIndex = (opcode == primitive::arrayAtPut) ? 2 : 1;
            Value* indexObject = getArgument(jit, argIndex--); // jit.popValue();
            Value* arrayObject = getArgument(jit, argIndex--); // jit.popValue();
            Value* valueObejct = (opcode == primitive::arrayAtPut) ? getArgument(jit, argIndex--) : 0;

            BasicBlock* indexChecked = BasicBlock::Create(m_JITModule->getContext(), "indexChecked.", jit.function);

            //Checking whether index is Smallint //TODO jump to primitiveFailed if not
            Value* indexIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, indexObject);

            Value* index       = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, indexObject);
            Value* actualIndex = jit.builder->CreateSub(index, jit.builder->getInt32(1));

            //Checking boundaries
            Value* arraySize   = jit.builder->CreateCall(m_baseFunctions.getObjectSize, arrayObject);
            Value* indexGEZero = jit.builder->CreateICmpSGE(actualIndex, jit.builder->getInt32(0));
            Value* indexLTSize = jit.builder->CreateICmpSLT(actualIndex, arraySize);
            Value* boundaryOk  = jit.builder->CreateAnd(indexGEZero, indexLTSize);

            Value* indexOk = jit.builder->CreateAnd(indexIsSmallInt, boundaryOk);
            jit.builder->CreateCondBr(indexOk, indexChecked, primitiveFailedBB);
            jit.builder->SetInsertPoint(indexChecked);

            if (opcode == primitive::arrayAtPut) {
                Function* getObjectFieldPtr = m_JITModule->getFunction("getObjectFieldPtr");
                Value* fieldPointer = jit.builder->CreateCall2(getObjectFieldPtr, arrayObject, actualIndex);
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
            Value* indexObject = getArgument(jit, argIndex--); // jit.popValue();
            Value* stringObject = getArgument(jit, argIndex--); // jit.popValue();
            Value* valueObejct = (opcode == primitive::stringAtPut) ? getArgument(jit, argIndex--) : 0;

            BasicBlock* indexChecked = BasicBlock::Create(m_JITModule->getContext(), "indexChecked.", jit.function);

            //Checking whether index is Smallint //TODO jump to primitiveFailed if not
            Value* indexIsSmallInt = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, indexObject);

            // Acquiring integer value of the index (from the smalltalk's TInteger)
            Value*    index    = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, indexObject);
            Value* actualIndex = jit.builder->CreateSub(index, jit.builder->getInt32(1));

            //Checking boundaries
            Value* stringSize  = jit.builder->CreateCall(m_baseFunctions.getObjectSize, stringObject);
            Value* indexGEZero = jit.builder->CreateICmpSGE(actualIndex, jit.builder->getInt32(0));
            Value* indexLTSize = jit.builder->CreateICmpSLT(actualIndex, stringSize);
            Value* boundaryOk  = jit.builder->CreateAnd(indexGEZero, indexLTSize);

            Value* indexOk = jit.builder->CreateAnd(indexIsSmallInt, boundaryOk, "indexOk.");
            jit.builder->CreateCondBr(indexOk, indexChecked, primitiveFailedBB);
            jit.builder->SetInsertPoint(indexChecked);

            // Getting access to the actual indexed byte location
            Value* fields    = jit.builder->CreateCall(m_baseFunctions.getObjectFields, stringObject);
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
            Value* rightObject = getArgument(jit, 1); // jit.popValue();
            Value* leftObject  = getArgument(jit, 0); // jit.popValue();
            compileSmallIntPrimitive(jit, opcode, leftObject, rightObject, primitiveResult, primitiveFailedBB);
        } break;

        case primitive::bulkReplace: {
            Value* destination            = getArgument(jit, 4); // jit.popValue();
            Value* sourceStartOffset      = getArgument(jit, 3); // jit.popValue();
            Value* source                 = getArgument(jit, 2); // jit.popValue();
            Value* destinationStopOffset  = getArgument(jit, 1); // jit.popValue();
            Value* destinationStartOffset = getArgument(jit, 0); // jit.popValue();

            Value* arguments[]  = {
                destination,
                destinationStartOffset,
                destinationStopOffset,
                source,
                sourceStartOffset
            };

            Value* isBulkReplaceSucceeded  = jit.builder->CreateCall(m_runtimeAPI.bulkReplace, arguments, "ok.");
            primitiveResult = destination;
            primitiveFailed = jit.builder->CreateNot(isBulkReplaceSucceeded);
        } break;

        case primitive::LLVMsendMessage: {
            Value* args     = jit.builder->CreateBitCast( getArgument(jit, 1) /*jit.popValue()*/, m_baseTypes.objectArray->getPointerTo() );
            Value* selector = jit.builder->CreateBitCast( getArgument(jit, 0) /*jit.popValue()*/, m_baseTypes.symbol->getPointerTo() );
            Value* context  = jit.getCurrentContext();

            Value* sendMessageArgs[] = {
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
            uint8_t argumentsCount = jit.currentNode->getInstruction().getArgument();
            Value* argumentsObject    = createArray(jit, argumentsCount);

            // Filling object with contents
            uint8_t index = argumentsCount;
            while (index > 0) {
                Value* value = getArgument(jit); // jit.popValue();
                jit.builder->CreateCall3(m_baseFunctions.setObjectField, argumentsObject, jit.builder->getInt32(--index), value);
            }

            Value* argumentsArray = jit.builder->CreateBitCast(argumentsObject, m_baseTypes.objectArray->getPointerTo());
            Value* primitiveFailedPtr = jit.builder->CreateAlloca(jit.builder->getInt1Ty(), 0, "primitiveFailedPtr");
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
    Value* rightIsInt  = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, rightObject);
    Value* leftIsInt   = jit.builder->CreateCall(m_baseFunctions.isSmallInteger, leftObject);
    Value* areIntsCond = jit.builder->CreateAnd(rightIsInt, leftIsInt);

    BasicBlock* areIntsBB = BasicBlock::Create(m_JITModule->getContext(), "areInts", jit.function);
    jit.builder->CreateCondBr(areIntsCond, areIntsBB, primitiveFailedBB);

    jit.builder->SetInsertPoint(areIntsBB);
    Value* rightOperand = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, rightObject);
    Value* leftOperand  = jit.builder->CreateCall(m_baseFunctions.getIntegerValue, leftObject);

    switch(opcode) {
        case primitive::smallIntAdd: {
            Value* intResult = jit.builder->CreateAdd(leftOperand, rightOperand);
            //FIXME overflow
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntDiv: {
            Value*      isZero = jit.builder->CreateICmpEQ(rightOperand, jit.builder->getInt32(0));
            BasicBlock* divBB  = BasicBlock::Create(m_JITModule->getContext(), "div", jit.function);
            jit.builder->CreateCondBr(isZero, primitiveFailedBB, divBB);

            jit.builder->SetInsertPoint(divBB);
            Value* intResult = jit.builder->CreateSDiv(leftOperand, rightOperand);
            primitiveResult  = jit.builder->CreateCall(m_baseFunctions.newInteger, intResult);
        } break;
        case primitive::smallIntMod: {
            Value*      isZero = jit.builder->CreateICmpEQ(rightOperand, jit.builder->getInt32(0));
            BasicBlock* modBB  = BasicBlock::Create(m_JITModule->getContext(), "mod", jit.function);
            jit.builder->CreateCondBr(isZero, primitiveFailedBB, modBB);

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
            jit.builder->CreateCondBr(shiftLeftFailed, primitiveFailedBB, shiftResultBB);

            jit.builder->SetInsertPoint(shiftResultBB);
            PHINode* phi = jit.builder->CreatePHI(jit.builder->getInt32Ty(), 2);
            phi->addIncoming(shiftRightResult, shiftRightBB);
            phi->addIncoming(shiftLeftResult, shiftLeftBB);

            primitiveResult = jit.builder->CreateCall(m_baseFunctions.newInteger, phi);
        } break;
    }
}

MethodCompiler::TStackObject MethodCompiler::allocateStackObject(llvm::IRBuilder<>& builder, uint32_t baseSize, uint32_t fieldsCount)
{
    // Storing current edit location
    BasicBlock* insertBlock = builder.GetInsertBlock();
    BasicBlock::iterator insertPoint = builder.GetInsertPoint();

    // Switching to the preamble
    BasicBlock* preamble = insertBlock->getParent()->begin();
    builder.SetInsertPoint(preamble, preamble->begin());

    // Allocating the object slot
    const uint32_t  holderSize = baseSize + sizeof(TObject*) * fieldsCount;
    AllocaInst* objectSlot = builder.CreateAlloca(builder.getInt8Ty(), builder.getInt32(holderSize));
    objectSlot->setAlignment(4);

    // Allocating object holder in the preamble
    AllocaInst* objectHolder = builder.CreateAlloca(m_baseTypes.object->getPointerTo(), 0, "stackHolder.");

    // Initializing holder with null value
//    builder.CreateStore(ConstantPointerNull::get(m_baseTypes.object->getPointerTo()), objectHolder, true);

    Function* gcrootIntrinsic = getDeclaration(m_JITModule, Intrinsic::gcroot);

    //Value* structData = { ConstantInt::get(builder.getInt1Ty(), 1) };

    // Registering holder in GC and supplying metadata that tells GC to treat this particular root
    // as a pointer to a stack object. Stack objects are not moved by GC. Instead, only their fields
    // and class pointer are updated.
    //Value* metaData = ConstantStruct::get(m_JITModule->getTypeByName("TGCMetaData"), ConstantInt::get(builder.getInt1Ty(), 1));
    Value* metaData = m_JITModule->getGlobalVariable("stackObjectMeta");
    Value* stackRoot = builder.CreateBitCast(objectHolder, builder.getInt8PtrTy()->getPointerTo());
    builder.CreateCall2(gcrootIntrinsic, stackRoot, builder.CreateBitCast(metaData, builder.getInt8PtrTy()));

    // Returning to the original edit location
    builder.SetInsertPoint(insertBlock, insertPoint);

    // Storing the address of stack object to the holder
    Value* newObject = builder.CreateBitCast(objectSlot, m_baseTypes.object->getPointerTo());
    builder.CreateStore(newObject, objectHolder/*, true*/);

    TStackObject result;
    result.objectHolder = objectHolder;
    result.objectSlot = objectSlot;
    return result;
}
