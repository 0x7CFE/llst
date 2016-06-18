#include <inference.h>
#include <visualization.h>

using namespace type;

static void printBlock(const Type& blockType, std::stringstream& stream) {
    if (blockType.getSubTypes().size() < Type::bstCaptureIndex) {
        stream << "(Block)";
        return;
    }

    TMethod* const method = blockType[Type::bstOrigin].getValue()->cast<TMethod>();
    const uint16_t offset = TInteger(blockType[Type::bstOffset].getValue());

    // Class>>method@offset#I[R][W]C
    stream
        << method->klass->name->toString()
        << ">>" << method->name->toString()
        << "@" << offset                                       // block offset within a method
        << "#" << blockType[Type::bstContextIndex].toString()  // creating context index
        << blockType[Type::bstReadsTemps].toString()           // read temporaries indices
        << blockType[Type::bstWritesTemps].toString();         // write temporaries indices

    if (blockType.getSubTypes().size() > Type::bstCaptureIndex)
        stream << blockType[Type::bstCaptureIndex].toString(); // capture context index
}

std::string Type::toString(bool subtypesOnly /*= false*/) const {
    std::stringstream stream;

    switch (m_kind) {
        case tkUndefined: return "?";
        case tkPolytype:  return "*";

        case tkLiteral:
            if (isSmallInteger(getValue()))
                stream << TInteger(getValue()).getValue();
            else if (getValue() == globals.nilObject)
                return "nil";
            else if (getValue() == globals.trueObject)
                return "true";
            else if (getValue() == globals.falseObject)
                return "false";
            else if (getValue()->getClass() == globals.stringClass)
                stream << "'" << getValue()->cast<TString>()->toString() << "'";
            else if (getValue()->getClass() == globals.badMethodSymbol->getClass())
                stream << "#" << getValue()->cast<TSymbol>()->toString();
            else if (getValue()->getClass()->name->toString().find("Meta", 0, 4) != std::string::npos)
                stream << getValue()->cast<TClass>()->name->toString();
            else if (getValue()->getClass() == globals.stringClass->getClass()->getClass())
                stream << getValue()->cast<TClass>()->name->toString();
            else
                stream << "~" << getValue()->getClass()->name->toString();
            break;

        case tkMonotype:
            if (getValue() == globals.blockClass)
                printBlock(*this, stream);
            else
                stream << "(" << getValue()->cast<TClass>()->name->toString() << ")";
            break;

        case tkArray:
            if (subtypesOnly)
                stream << getValue()->cast<TClass>()->name->toString();
        case tkComposite: {
            stream << (m_kind == tkComposite ? "(" : "[");

            for (std::size_t index = 0; index < getSubTypes().size(); index++) {
                if (index)
                    stream << ", ";

                stream << m_subTypes[index].toString();
            }

            stream << (m_kind == tkComposite ? ")" : "]");
        };
    }

    return stream.str();
}

void TypeAnalyzer::fillLinkerClosures() {
    typedef InferContext::TBlockClosures::const_iterator TClosureIterator;
    InferContext::TBlockClosures& closures = m_context.getBlockClosures();

    for (TClosureIterator iClosure = closures.begin(); iClosure != closures.end(); ++iClosure) {
        const InferContext::TSiteIndex siteIndex = iClosure->first;
        InstructionNode* const instruction = m_siteMap[siteIndex];

        if (!instruction) {
            std::cout << "site index " << siteIndex << " is not found" << std::endl;
            continue;
        }

        Type& arguments = m_context[*instruction->getArgument()];

        std::cout
            << "analyzing potential closure args for " << instruction->getIndex() << " :: "
            << arguments.toString() << std::endl;

        for (std::size_t argIndex = 0; argIndex < arguments.getSubTypes().size(); argIndex++) {
            Type& blockType = arguments[argIndex];

//             std::cout
//                 << "analyzing potential closure arg " << argIndex << " :: "
//                 << blockType.toString() << std::endl;

            if (blockType.getValue() != globals.blockClass || blockType.getKind() != Type::tkMonotype)
                continue; // Not a block we may handle

            if (blockType.getSubTypes().empty())
                continue; // Non-literal or non-local block

            const Type& blockReadIndices  = blockType[Type::bstReadsTemps];
            const Type& blockWrtieIndices = blockType[Type::bstWritesTemps];

            ClosureTauNode::TIndexList readIndices;
            ClosureTauNode::TIndexList wrtieIndices;

            readIndices.reserve(blockReadIndices.getSubTypes().size());
            wrtieIndices.reserve(blockWrtieIndices.getSubTypes().size());

            for (std::size_t i = 0; i < blockReadIndices.getSubTypes().size(); i++) {
                const std::size_t variableIndex = TInteger(blockReadIndices[i].getValue());
                readIndices.push_back(variableIndex);
            }

            for (std::size_t i = 0; i < blockWrtieIndices.getSubTypes().size(); i++) {
                const std::size_t variableIndex = TInteger(blockWrtieIndices[i].getValue());
                wrtieIndices.push_back(variableIndex);
            }

            std::cout
                << "adding closure node " << siteIndex
                << " reads " << blockReadIndices.toString()
                << " writes " << blockWrtieIndices.toString()
                << std::endl;

            m_tauLinker.addClosureNode(*instruction, readIndices, wrtieIndices);
        }
    }
}

bool TypeAnalyzer::basicRun() {
    m_walker.resetStopNodes();
    m_walker.run(*m_graph.nodes_begin(), Walker::wdForward);

    Type& returnType = m_context.getReturnType();
    const bool singleReturn = (returnType.getSubTypes().size() == 1);

    if (singleReturn)
        returnType = returnType[0];
    else
        returnType.setKind(Type::tkComposite);

    return singleReturn;
}

std::string TypeAnalyzer::getMethodName() {
    TMethod* const method = m_graph.getParsedMethod()->getOrigin();
    std::ostringstream ss;
    ss << method->klass->name->toString() + ">>" + method->name->toString();
    if (m_blockType)
        ss << "@" << TInteger((*m_blockType)[Type::bstOffset].getValue()).getValue();

    return ss.str();
}


void TypeAnalyzer::run(const Type* blockType /*= 0*/) {
    if (m_graph.isEmpty())
        return;

    m_blockType = blockType;
    const std::string methodName = getMethodName();
    std::cout << "Initial pass on " << methodName << std::endl;

    m_baseRun = true;
    m_literalBranch = true;
    m_tauLinker.run();
    bool singleReturn = basicRun();

    if (m_literalBranch && singleReturn && !m_graph.getMeta().hasMutatingBlocks)
        return;

    TTypeMap controlTypes;

//     for (std::size_t passNumber = 0; ; passNumber++)
    std::size_t passNumber = 0;
    {
        if (m_graph.getMeta().hasLiteralBlocks) {
            m_tauLinker.reset();
            fillLinkerClosures();
            m_tauLinker.run();
            m_context.resetTypes();

            std::cout << "Base pass on " << methodName << " (" << passNumber << ")" << std::endl;
            m_baseRun = true;
            m_literalBranch = true;
            singleReturn = basicRun();

            if (m_literalBranch && singleReturn && !m_graph.getMeta().hasMutatingBlocks)
                return;
        }

        {
            ControlGraphVisualizer vis(&m_graph, methodName, "dots/");
            vis.run();
        }

        std::cout << "Induction pass on " << methodName << " (" << passNumber << ")" << std::endl;
        m_baseRun = false;
        m_literalBranch = true;
        //controlTypes = m_context.getTypes();
        singleReturn = basicRun();

        //if (controlTypes == m_context.getTypes())
            //break;
    }
}

/*void TypeAnalyzer::run_(const Type* blockType) {
    if (m_graph.isEmpty())
        return;

    m_blockType = blockType;

    // FIXME For correct inference we need to perform in-width traverse

    m_baseRun = true;
    m_literalBranch = true;
    m_walker.run(*m_graph.nodes_begin(), Walker::wdForward);

    std::cout << "Base run:" << std::endl;
    type::TTypeList::const_iterator iType = m_context.getTypes().begin();
    for (; iType != m_context.getTypes().end(); ++iType)
        std::cout << iType->first << " " << iType->second.toString() << std::endl;

    // If single return is detected, replace composite with it's subtype
    // TODO We may need to store actual receiver class somewhere
    Type& returnType = m_context.getReturnType();
    const bool singleReturn = (returnType.getSubTypes().size() == 1);

    if (singleReturn)
        returnType = returnType[0];
    else
        returnType.setKind(Type::tkComposite);

    std::cout << "return type: " << m_context.getReturnType().toString() << std::endl;

    if (m_literalBranch && singleReturn) {
        // std::cout << "Return path was inferred literally. No need to perform induction run." << std::endl;
        return;
    }

    if (m_graph.getMeta().hasBackEdgeTau) {
        m_baseRun = false;

        m_walker.resetStopNodes();
        m_walker.run(*m_graph.nodes_begin(), Walker::wdForward);

        std::cout << "Induction run:" << std::endl;
        type::TTypeList::const_iterator iType = m_context.getTypes().begin();
        for (; iType != m_context.getTypes().end(); ++iType)
            std::cout << iType->first << " " << iType->second.toString() << std::endl;

        if (returnType.getSubTypes().size() == 1)
            returnType = returnType[0];
        else
            returnType.setKind(Type::tkComposite);

        std::cout << "return type: " << m_context.getReturnType().toString() << std::endl;
    }
}*/

Type& TypeAnalyzer::getArgumentType(const InstructionNode& instruction, std::size_t index /*= 0*/) {
    ControlNode* const argNode = instruction.getArgument(index);
    Type& result = m_context[*argNode];

    if (PhiNode* const phi = argNode->cast<PhiNode>())
        result = processPhi(*phi);

    return result;
}

void TypeAnalyzer::processInstruction(InstructionNode& instruction) {
//     std::printf("processing %.2u\n", instruction.getIndex());

    switch (instruction.getInstruction().getOpcode()) {
        case opcode::pushConstant:    doPushConstant(instruction);    break;
        case opcode::pushLiteral:     doPushLiteral(instruction);     break;
        case opcode::pushArgument:    doPushArgument(instruction);    break;

        case opcode::pushTemporary:   doPushTemporary(instruction);   break;
        case opcode::assignTemporary: doAssignTemporary(instruction); break;

        case opcode::pushBlock:       doPushBlock(instruction);       break;

        case opcode::markArguments:   doMarkArguments(instruction);   break;
        case opcode::sendUnary:       doSendUnary(instruction);       break;
        case opcode::sendBinary:      doSendBinary(instruction);      break;
        case opcode::sendMessage:     doSendMessage(instruction);     break;

        case opcode::doPrimitive:     doPrimitive(instruction);       break;
        case opcode::doSpecial:       doSpecial(instruction);         break;

        default:
            break;
    }

//     std::printf("type of %.2u is %s\n", instruction.getIndex(), m_context[instruction].toString().c_str());
}

void TypeAnalyzer::doPushConstant(const InstructionNode& instruction) {
    const TSmalltalkInstruction::TArgument argument = instruction.getInstruction().getArgument();
    Type& type = m_context[instruction];

    switch (argument) {
        case 0: case 1: case 2: case 3: case 4:
        case 5: case 6: case 7: case 8: case 9:
            type.set(TInteger(argument));
            break;

        case pushConstants::nil:         type.set(globals.nilObject);   break;
        case pushConstants::trueObject:  type.set(globals.trueObject);  break;
        case pushConstants::falseObject: type.set(globals.falseObject); break;

        default:
            std::fprintf(stderr, "VM: unknown push constant %d\n", argument);
            type.reset();
    }
}

void TypeAnalyzer::doPushLiteral(const InstructionNode& instruction) {
    TMethod* const method  = m_graph.getParsedMethod()->getOrigin();
    const TSmalltalkInstruction::TArgument argument = instruction.getInstruction().getArgument();
    TObject* const literal = method->literals->getField(argument);

    m_context[instruction] = Type(literal);
}

void TypeAnalyzer::doPushArgument(const InstructionNode& instruction) {
    const TSmalltalkInstruction::TArgument index = instruction.getInstruction().getArgument();

    if (m_blockType) {
        if (InferContext* const methodContext = getMethodContext()) {
            m_context[instruction] = methodContext->getArgument(index);
        }
    } else {
        m_context[instruction] = m_context.getArgument(index);
    }
}

void TypeAnalyzer::doSendUnary(const InstructionNode& instruction) {
    const Type& argType = m_context[*instruction.getArgument()];
    const unaryBuiltIns::Opcode opcode = static_cast<unaryBuiltIns::Opcode>(instruction.getInstruction().getArgument());

    Type& result = m_context[instruction];
    switch (argType.getKind()) {
        case Type::tkLiteral:
        case Type::tkMonotype:
        {
            const bool isValueNil =
                   (argType.getValue() == globals.nilObject)
                || (argType.getValue() == globals.nilObject->getClass());

            if (opcode == unaryBuiltIns::isNil)
                result.set(isValueNil ? globals.trueObject : globals.falseObject);
            else
                result.set(isValueNil ? globals.falseObject : globals.trueObject);
            break;
        }

        case Type::tkComposite:
        case Type::tkArray:
        {
            // TODO Repeat the procedure over each subtype
            result = Type(Type::tkPolytype);
            break;
        }

        default:
            // * isNil  -> (Boolean)
            // * notNil -> (Boolean)
            result.set(globals.trueObject->getClass()->parentClass);
    }

}

void TypeAnalyzer::doSendBinary(InstructionNode& instruction) {
    const Type& lhsType = m_context[*instruction.getArgument(0)];
    const Type& rhsType = m_context[*instruction.getArgument(1)];
    const binaryBuiltIns::Operator opcode = static_cast<binaryBuiltIns::Operator>(instruction.getInstruction().getArgument());

    Type& result = m_context[instruction];

    if (isSmallInteger(lhsType.getValue()) && isSmallInteger(rhsType.getValue())) {
        if (!m_baseRun)
            return;

        const int32_t leftOperand  = TInteger(lhsType.getValue());
        const int32_t rightOperand = TInteger(rhsType.getValue());

        switch (opcode) {
            case binaryBuiltIns::operatorLess:
                result.set((leftOperand < rightOperand) ? globals.trueObject : globals.falseObject);
                break;

            case binaryBuiltIns::operatorLessOrEq:
                result.set((leftOperand <= rightOperand) ? globals.trueObject : globals.falseObject);
                break;

            case binaryBuiltIns::operatorPlus:
                result.set(TInteger(leftOperand + rightOperand));
                break;

            default:
                std::fprintf(stderr, "VM: Invalid opcode %d passed to sendBinary\n", opcode);
        }

        return;
    }

    // Literal int or (SmallInt) monotype
    const bool isInt1 = isSmallInteger(lhsType.getValue()) || lhsType.getValue() == globals.smallIntClass;
    const bool isInt2 = isSmallInteger(rhsType.getValue()) || rhsType.getValue() == globals.smallIntClass;

    if (isInt1 && isInt2) {
        switch (opcode) {
            case binaryBuiltIns::operatorLess:
            case binaryBuiltIns::operatorLessOrEq:
                // (SmallInt) <= (SmallInt) -> (Boolean)
                result.set(globals.trueObject->getClass()->parentClass);
                break;

            case binaryBuiltIns::operatorPlus:
                // (SmallInt) + (SmallInt) -> (SmallInt)
                result.set(globals.smallIntClass);
                break;

            default:
                std::fprintf(stderr, "VM: Invalid opcode %d passed to sendBinary\n", opcode);
                result.reset(); // ?
        }

        return;
    }

    // In case of complex invocation encode resursive analysis of operator as a message
    TSymbol* const selector = globals.binaryMessages[opcode]->cast<TSymbol>();

    Type arguments(Type::tkArray);
    arguments.pushSubType(lhsType);
    arguments.pushSubType(rhsType);

    captureContext(instruction, arguments);

    if (InferContext* const context = m_system.inferMessage(selector, arguments, &m_contextStack))
        result = context->getReturnType();
    else
        result = Type(Type::tkPolytype);
}

void TypeAnalyzer::doMarkArguments(const InstructionNode& instruction) {
    Type& result = m_context[instruction];

    if (!m_baseRun)
        result.reset();

    for (std::size_t index = 0; index < instruction.getArgumentsCount(); index++) {
        const Type& argument = m_context[*instruction.getArgument(index)];
        result.pushSubType(argument);
    }

    result.set(globals.arrayClass, Type::tkArray);
}

InferContext* TypeAnalyzer::getMethodContext() {
    assert(m_blockType);

    for (TContextStack* stack = m_contextStack.parent; stack; stack = stack->parent) {
        const TInteger contextIndex((*m_blockType)[Type::bstContextIndex].getValue());

        if (stack->context.getIndex() == static_cast<std::size_t>(contextIndex))
            return &stack->context;
    }

    return 0;
}

void TypeAnalyzer::doPushTemporary(const InstructionNode& instruction) {
    if (TauNode* const tau = instruction.getTauNode()) {
        switch (tau->getKind()) {
            case TauNode::tkAggregator:
                processTau(*tau);
            case TauNode::tkProvider:
                m_context[instruction] = m_context[*tau];
                break;

            case TauNode::tkUnknown:
                std::fprintf(stderr, "Unknown tau node index %u found from %u\n", tau->getIndex(), instruction.getIndex());
                assert(false);
                break;

            case TauNode::tkClosure: {
                const ClosureTauNode* const closureTau = tau->cast<ClosureTauNode>();
                const uint32_t closureIndex = closureTau->getOrigin()->getIndex();
                const uint16_t tempIndex = instruction.getInstruction().getArgument();

                m_context[instruction] = m_context.getBlockClosures()[closureIndex][tempIndex];
                break;
            }
        }
    } else if (m_blockType) {
        const uint16_t argIndex  = TInteger((*m_blockType)[Type::bstArgIndex].getValue());
        const uint16_t tempIndex = instruction.getInstruction().getArgument();

        if (tempIndex >= argIndex) {
            m_context[instruction] = m_context.getArgument(tempIndex - argIndex);
        } else {
            if (InferContext* const methodContext = getMethodContext()) {
                const TInteger captureIndex((*m_blockType)[Type::bstCaptureIndex].getValue());
                InferContext::TVariableMap& closureTypes = methodContext->getBlockClosures()[captureIndex];

                m_context[instruction] = closureTypes[tempIndex];
            }
        }
    } else {
        // Method variables are initialized to nil by default
        m_context[instruction] = Type(globals.nilObject);
    }
}

void TypeAnalyzer::doAssignTemporary(const InstructionNode& instruction) {
    const TauNode* const tau = instruction.getTauNode();
    assert(tau);
    assert(tau->getKind() == TauNode::tkProvider);

    const ControlNode* const argument = instruction.getArgument();
    m_context[*tau] = m_context[*argument];

    if (!m_blockType)
        return;

    InferContext* const methodContext = getMethodContext();
    if (!methodContext) {
        std::cout << "method context not found for " << instruction.getIndex() << std::endl;
        return;
    }

    const TInteger captureIndex((*m_blockType)[Type::bstCaptureIndex].getValue());

    InferContext::TVariableMap& closureTypes = methodContext->getBlockClosures()[captureIndex];
    const uint16_t tempIndex = instruction.getInstruction().getArgument();

    InferContext::TVariableMap::iterator iType = closureTypes.find(tempIndex);
    if (iType == closureTypes.end())
        closureTypes[tempIndex]  = m_context[*argument];
    else
        closureTypes[tempIndex] &= m_context[*argument];

    std::cout
        << "doAssignTemporary, writing temporary " << tempIndex
        << " in closure " << captureIndex.getValue()
        << " type & " << m_context[*argument].toString()
        << " = " << closureTypes[tempIndex].toString()
        << std::endl;
}

void TypeAnalyzer::doPushBlock(const InstructionNode& instruction) {
    if (const PushBlockNode* const pushBlock = instruction.cast<PushBlockNode>()) {
        TMethod* const origin = pushBlock->getParsedBlock()->getContainer()->getOrigin();
        const uint16_t offset = pushBlock->getParsedBlock()->getStartOffset();
        const uint16_t argIndex = instruction.getInstruction().getArgument();

        Type& blockType = m_context[instruction];

        if (!blockType.getSubTypes().empty())
            return; // Already processed before

        m_graph.getMeta().hasLiteralBlocks = true;

        blockType.set(globals.blockClass, Type::tkMonotype);
        blockType.pushSubType(origin);                                 // [Type::bstOrigin]
        blockType.pushSubType(Type(TInteger(offset)));                 // [Type::bstOffset]
        blockType.pushSubType(Type(TInteger(argIndex)));               // [Type::bstArgIndex]
        blockType.pushSubType(Type(TInteger(m_context.getIndex())));   // [Type::bstContextIndex]

        // TODO Cache and reuse in TypeSystem::inferBlock()
        ControlGraph* const blockGraph = new ControlGraph(pushBlock->getParsedBlock()->getContainer(), pushBlock->getParsedBlock());
        blockGraph->buildGraph();

        typedef ControlGraph::TMetaInfo::TIndexList TIndexList;
        const TIndexList& readsTemporaries  = blockGraph->getMeta().readsTemporaries;
        const TIndexList& writesTemporaries = blockGraph->getMeta().writesTemporaries;

        if (writesTemporaries.size())
            m_graph.getMeta().hasMutatingBlocks = true;

        Type readIndices(globals.arrayClass, Type::tkArray);
        Type writeIndices(globals.arrayClass, Type::tkArray);

        for (std::size_t index = 0; index < readsTemporaries.size(); index++) {
            // We're interested only in temporaries from lexical context, not block arguments
            if (readsTemporaries[index] >= argIndex)
                continue;

            readIndices.pushSubType(Type(TInteger(readsTemporaries[index])));
        }

        for (std::size_t index = 0; index < writesTemporaries.size(); index++) {
            // We're interested only in temporaries from lexical context, not block arguments
            if (writesTemporaries[index] >= argIndex)
                continue;

            writeIndices.pushSubType(Type(TInteger(writesTemporaries[index])));
        }

        blockType.pushSubType(readIndices);  // [Type::bstReadsTemps]
        blockType.pushSubType(writeIndices); // [Type::bstWritesTemps]
    }
}

class TypeLocator : public GraphWalker {
public:
    TypeLocator(std::size_t tempIndex, const ControlGraph::TEdgeSet& backEdges, InferContext& context, bool noBackEdges)
        : m_tempIndex(tempIndex), m_backEdges(backEdges), m_context(context), m_noBackEdges(noBackEdges), m_firstResult(true) {}

    const Type& getResult() const { return m_result; }

private:
    virtual TVisitResult visitNode(st::ControlNode& node, const TPathNode* path) {
        if (InstructionNode* const instruction = node.cast<InstructionNode>()) {
            switch (instruction->getInstruction().getOpcode()) {
                case opcode::assignTemporary:
                    return checkAssignTemporary(*instruction, path);

                case opcode::sendBinary:
                case opcode::sendMessage:
                    return checkClosure(*instruction, path);

                default:
                    break;
            }
        }

        return vrKeepWalking;
    }

    bool containsBackEdge(const TPathNode* path) const {
        // Search for back edges in the located path

        for (const TPathNode* p = path; p->prev; p = p->prev) {
            const ControlGraph::TEdgeSet::const_iterator iEdge = m_backEdges.find(
                st::BackEdgeDetector::TEdge(
                    static_cast<const st::InstructionNode*>(p->node),
                    static_cast<const st::InstructionNode*>(p->prev->node)
                )
            );

            if (iEdge != m_backEdges.end())
                return true;
        }

        return false;
    }

    TVisitResult checkAssignTemporary(InstructionNode& instruction, const TPathNode* path) {
        if (instruction.getInstruction().getArgument() != m_tempIndex)
            return vrKeepWalking;

        TauNode* const tau = instruction.getTauNode();
        if (! tau)
            return vrKeepWalking;

        // Searching for back edges in the located path
        const bool viaBackEdge = containsBackEdge(path);

        std::printf("TypeLocator : Found assign site: Node %.2u :: %s, back edge: %s\n",
                    instruction.getIndex(),
                    m_context[*tau].toString().c_str(),
                    viaBackEdge ? "yes" : "no");

        if (viaBackEdge && m_noBackEdges)
            return vrSkipPath;

        if (m_firstResult) {
            m_result = m_context[*tau];
            m_firstResult = false;
        } else {
            m_result &= m_context[*tau];
        }

        return vrSkipPath;
    }

    TVisitResult checkClosure(InstructionNode& instruction, const TPathNode* path) {
        std::cout << "checkClosure " << instruction.getIndex() << std::endl;

        const bool viaBackEdge = containsBackEdge(path);

        if (viaBackEdge && m_noBackEdges)
            return vrSkipPath;

        const InferContext::TBlockClosures& closures = m_context.getBlockClosures();
        InferContext::TBlockClosures::const_iterator iClosure = closures.find(instruction.getIndex());

        if (iClosure == closures.end()) {
            std::cout << "checkClosure " << instruction.getIndex() << " is not found" << std::endl;
            return vrKeepWalking;
        }

        // FIXME Currently we link to a closure even if it is not mutating temporaries

        const InferContext::TVariableMap& closureVariables = iClosure->second;
        InferContext::TVariableMap::const_iterator iType = closureVariables.find(m_tempIndex);

        if (iType == closureVariables.end()) {
            std::cout << "checkClosure " << instruction.getIndex() << " no variable" << std::endl;
            return vrKeepWalking;
        }

        std::printf("TypeLocator : Found closure site: Node %.2u :: %s, back edge: %s\n",
                    instruction.getIndex(),
                    iType->second.toString().c_str(),
                    viaBackEdge ? "yes" : "no");

        if (m_firstResult) {
            m_result = iType->second;
            m_firstResult = false;
        } else {
            m_result &= iType->second;
        }

        return vrSkipPath;
    }

private:
    const TSmalltalkInstruction::TArgument m_tempIndex;
    const ControlGraph::TEdgeSet& m_backEdges;
    InferContext& m_context;
    const bool m_noBackEdges;

    Type m_result;
    bool m_firstResult;
};

void TypeAnalyzer::captureContext(InstructionNode& instruction, Type& arguments) {
    TMethod* const currentMethod = m_graph.getParsedMethod()->getOrigin();

//     std::cout
//         << "captureContext of " << instruction.getIndex()
//         << ": Analyzing capture arguments "
//         << arguments.toString() << std::endl;

    // We interested in literal blocks with context info from the same method
    for (std::size_t argIndex = 0; argIndex < arguments.getSubTypes().size(); argIndex++) {
        Type& blockType = arguments[argIndex];

        if (blockType.getValue() != globals.blockClass || blockType.getKind() != Type::tkMonotype)
            continue; // Not a block we may handle

        if (blockType.getSubTypes().empty() || blockType[Type::bstOrigin].getValue() != currentMethod)
            continue; // Non-literal or non-local block

        if (TInteger(blockType[Type::bstContextIndex].getValue()) != static_cast<int32_t>(m_context.getIndex()))
            continue; // Block was created in another method (non-local)

        std::cout
            << "captureContext of " << instruction.getIndex()
            << " : Analyzing block " << blockType.toString() << std::endl;

        // Index of the capture site
        const InferContext::TSiteIndex siteIndex = instruction.getIndex();
        //FIXME if (blockType[Type::bstReadsTemps].getSubTypes().size())
        {
            std::cout << "writing siteIndex: " << siteIndex << std::endl;
            blockType.pushSubType(Type(TInteger(siteIndex))); // [Type::bstCaptureIndex]

            std::cout << "writing " << &instruction << " as site index " << siteIndex << std::endl;
            m_siteMap[siteIndex] = &instruction;
        }

        // Indexes of temporaries from the lexical context. See TypeAnalyzer::pushBlock()
        const Type& readIndices = blockType[Type::bstReadsTemps];

        // Prepare captured context by writing inferred variable types at the capture point
        for (std::size_t i = 0; i < readIndices.getSubTypes().size(); i++) {
            const std::size_t variableIndex = TInteger(readIndices[i].getValue());

            // TODO Move out of the loop
            TypeLocator locator(
                variableIndex, // look for temporary with this index
                m_graph.getMeta().backEdges,
                m_context,
                m_baseRun      // base run = skip back edges
            );

            // Detect types of temporaries accessible from the current call site
            locator.run(&instruction, st::GraphWalker::wdBackward, false);

            InferContext::TVariableMap& typeMap = m_context.getBlockClosures()[siteIndex];
            InferContext::TVariableMap::iterator iType = typeMap.find(variableIndex);

            if (iType != typeMap.end())
                iType->second &= locator.getResult();
            else
                typeMap[variableIndex] = locator.getResult();

        }
    }
}

void TypeAnalyzer::doSendMessage(InstructionNode& instruction) {
    TSymbolArray&  literals     = *m_graph.getParsedMethod()->getOrigin()->literals;
    const uint32_t literalIndex = instruction.getInstruction().getArgument();

    TSymbol* const selector = literals[literalIndex];
    Type& arguments = m_context[*instruction.getArgument()];

    captureContext(instruction, arguments);

    Type& result = m_context[instruction];
    if (InferContext* const context = m_system.inferMessage(selector, arguments, &m_contextStack))
        result = context->getReturnType();
    else
        result = Type(Type::tkPolytype);
}

void TypeAnalyzer::doPrimitive(const InstructionNode& instruction) {
    const uint8_t opcode = instruction.getInstruction().getExtra();

    Type primitiveResult;

    switch (opcode) {
        case primitive::allocateObject:
        case primitive::allocateByteArray:
        {
            const Type& klassType = m_context[*instruction.getArgument()];

            // instance <- Class new
            //
            // <7 Array 2> -> Array[nil, nil]
            // <7 Object 2> -> Object[nil, nil]
            // <7 Object (SmallInt)> -> (Object)
            // <7 Object *> -> (Object)
            // <7 (Class) 2> -> *[nil, nil] -> *
            // <7 * 2> -> *[nil, nil] -> *
            // <7 * (SmallInt)> -> *
            // <7 * *> -> *

            switch (klassType.getKind()) {
                case Type::tkLiteral:
                    // If we literally know the class we may define the instance's type
                    primitiveResult = Type(klassType.getValue(), Type::tkMonotype);
                    break;

                default:
                    // Otherwise it's completely unknown what will be the instance's type
                    primitiveResult = Type(Type::tkPolytype);
            }

            break;
        }

        case primitive::getClass: {
            const Type& selfType = m_context[*instruction.getArgument()];
            TObject* const self  = selfType.getValue();

            switch (selfType.getKind()) {
                case Type::tkLiteral: {
                    // Here class itself is a literal, not a monotype
                    TClass* selfClass = isSmallInteger(self) ? globals.smallIntClass : self->getClass();
                    primitiveResult = Type(selfClass, Type::tkLiteral);
                    break;
                }

                case Type::tkMonotype:
                    // (Object) class -> Object
                    primitiveResult = Type(self);
                    break;

                default: {
                    // String -> MetaString -> Class
                    // String class class = Class
                    // TClass* const classClass = globals.stringClass->getClass()->getClass();
                    // primitiveResult = Type(classClass, Type::tkMonotype);
                    primitiveResult = Type(Type::tkPolytype);
                }
            }

            break;
        }

        case primitive::getSize: {
            const Type& self = m_context[*instruction.getArgument(0)];

            if (self.getKind() == Type::tkLiteral) {
                TObject* const   value = self.getValue();
                const std::size_t size = isSmallInteger(value) ? 0 : value->getSize();

                primitiveResult = Type(TInteger(size));
            } else {
                primitiveResult = Type(globals.smallIntClass);
            }

            // TODO What about Monotype and TCLass::instanceSize?
            //      Will not work for binary objects.

            break;
        }

        case primitive::blockInvoke: {
            Type& block = m_context[*instruction.getArgument(0)];
            Type arguments(Type::tkArray);

            if (instruction.getArgumentsCount() == 2)
                arguments.pushSubType(m_context[*instruction.getArgument(1)]);

            if (instruction.getArgumentsCount() == 3)
                arguments.pushSubType(m_context[*instruction.getArgument(2)]);

            if (InferContext* const blockContext = m_system.inferBlock(block, arguments, &m_contextStack))
                primitiveResult = blockContext->getReturnType();
            else
                primitiveResult = Type(Type::tkPolytype);

            break;
        }

        case primitive::smallIntAdd:
        case primitive::smallIntSub:
        case primitive::smallIntMul:
        case primitive::smallIntDiv:
        case primitive::smallIntMod:
        case primitive::smallIntEqual:
        case primitive::smallIntLess:
        {
            const Type& self = m_context[*instruction.getArgument(0)];
            const Type& arg  = m_context[*instruction.getArgument(1)];

            if (isSmallInteger(self.getValue()) && isSmallInteger(arg.getValue())) {
                const int lhs = TInteger(self.getValue()).getValue();
                const int rhs = TInteger(arg.getValue()).getValue();

                switch (opcode) {
                    case primitive::smallIntAdd: primitiveResult = Type(TInteger(lhs + rhs)); break;
                    case primitive::smallIntSub: primitiveResult = Type(TInteger(lhs - rhs)); break;
                    case primitive::smallIntMul: primitiveResult = Type(TInteger(lhs * rhs)); break;
                    case primitive::smallIntDiv: primitiveResult = Type(TInteger(lhs / rhs)); break;
                    case primitive::smallIntMod: primitiveResult = Type(TInteger(lhs % rhs)); break;

                    case primitive::smallIntEqual:
                        primitiveResult = (lhs == rhs) ? globals.trueObject : globals.falseObject;
                        break;

                    case primitive::smallIntLess:
                        primitiveResult = (lhs < rhs) ? globals.trueObject : globals.falseObject;
                        break;
                }
            } else {
                // TODO Check for (SmallInt)
                primitiveResult = Type(Type::tkPolytype);
                // primitiveResult = Type(globals.smallIntClass, Type::tkMonotype);
            }

            break;
        }
    }

    m_context.getReturnType().addSubType(primitiveResult);

    // This should depend on the primitive inference outcome
    m_walker.addStopNode(*instruction.getOutEdges().begin());
}

void TypeAnalyzer::doSpecial(const InstructionNode& instruction) {
    const TSmalltalkInstruction::TArgument argument = instruction.getInstruction().getArgument();

    switch (argument) {
        case special::branchIfFalse:
        case special::branchIfTrue: {
            const bool branchIfTrue = (argument == special::branchIfTrue);
            const Type& argType = m_context[*instruction.getArgument()];

            const BranchNode* const branch = instruction.cast<BranchNode>();
            assert(branch);

            if (argType.getValue() == globals.trueObject || argType.getValue() == globals.trueObject->getClass())
                m_walker.addStopNode(branchIfTrue ? branch->getSkipNode() : branch->getTargetNode());
            else if (argType.getValue() == globals.falseObject || argType.getValue() == globals.falseObject->getClass())
                m_walker.addStopNode(branchIfTrue ? branch->getTargetNode() : branch->getSkipNode());
            else
                m_literalBranch = false;

            break;
        }

        case special::stackReturn:
            m_context.getReturnType().addSubType(getArgumentType(instruction));
            break;

        case special::selfReturn:
            m_context.getReturnType().addSubType(m_context.getArgument(0));
            break;

        case special::sendToSuper:
            // For now, treat method call as *
            m_context[instruction] = Type(Type::tkPolytype);
            break;

        case special::duplicate:
            m_context[instruction] = m_context[*instruction.getArgument()];
            break;
    }
}

Type& TypeAnalyzer::processPhi(const PhiNode& phi) {
    Type& result = m_context[phi];

    const TNodeSet& incomings = phi.getRealValues();
    TNodeSet::iterator iNode = incomings.begin();
    for (; iNode != incomings.end(); ++iNode) {
        // FIXME We need to track the source of the phi's incoming.
        //       We may ignore tkUndefined only if node lies on the dead path.

        result |= m_context[*(*iNode)->cast<InstructionNode>()];
    }

    return result;
}

void TypeAnalyzer::processTau(const TauNode& tau) {
    Type& result = m_context[tau];

    const TauNode::TIncomingMap& incomings = tau.getIncomingMap();
    TauNode::TIncomingMap::const_iterator iNode = incomings.begin();

    bool typeAssigned = false;

    for (; iNode != incomings.end(); ++iNode) {
        const bool byBackEdge = iNode->second;
        if (byBackEdge && m_baseRun)
            continue;

        std::cout << "Adding subtype to " << tau.getIndex() << " : " << m_context[*iNode->first].toString() << std::endl;

        if (! typeAssigned) {
            result = m_context[*iNode->first];
            typeAssigned = true;
        } else {
            result &= m_context[*iNode->first];
        }
    }

    std::cout << "Tau value is " << result.toString() << std::endl;
}

void TypeAnalyzer::walkComplete() {
//     std::printf("walk complete\n");
}

ControlGraph* TypeSystem::getControlGraph(TMethod* method) {
    TGraphCache::iterator iGraph = m_graphCache.find(method);
    if (iGraph != m_graphCache.end())
        return iGraph->second.second;

    ParsedMethod* const parsedMethod = new ParsedMethod(method);
    ControlGraph* const controlGraph = new ControlGraph(parsedMethod);
    controlGraph->buildGraph();

    TGraphEntry& entry = m_graphCache[method];
    entry.first  = parsedMethod;
    entry.second = controlGraph;

//     {
//         std::ostringstream ss;
//         ss << method->klass->name->toString() + ">>" + method->name->toString();
//
//         ControlGraphVisualizer vis(controlGraph, ss.str(), "dots/");
//         vis.run();
//     }

    return controlGraph;
}

InferContext* TypeSystem::inferMessage(TSelector selector, const Type& arguments, TContextStack* parent) {
    if (!selector || arguments.getKind() != Type::tkArray || arguments.getSubTypes().empty())
        return 0;

    const Type& self = arguments[0];

    switch (self.getKind()) {
        case Type::tkUndefined:
        case Type::tkPolytype:
            return 0;

        // TODO Handle the case when self is a composite type
        case Type::tkComposite:
            return 0;

        case Type::tkLiteral:
        case Type::tkMonotype:
        case Type::tkArray:
            break;
    }

    TContextMap& contextMap = m_contextCache[selector];

    const TContextMap::iterator iContext = contextMap.find(arguments);
    if (iContext != contextMap.end())
        return iContext->second;

    TClass* receiver = 0;

    if (self.getKind() == Type::tkLiteral) {
        if (isSmallInteger(self.getValue()))
            receiver = globals.smallIntClass;
        else if (self.getValue()->getClass()->getClass() == globals.stringClass->getClass()->getClass())
            receiver = self.getValue()->cast<TClass>();
        else
            receiver = self.getValue()->getClass();
    } else {
        receiver = self.getValue()->cast<TClass>();
    }

    TMethod* const method = m_vm.lookupMethod(selector, receiver);

    if (! method) // TODO Redirect to #doesNotUnderstand: statically
        return 0;

    InferContext* const inferContext = new InferContext(method, m_lastContextIndex++, arguments);
    contextMap[arguments] = inferContext;

    std::printf("Analyzing %s::%s>>%s...\n",
                arguments.toString().c_str(),
                method->klass->name->toString().c_str(),
                selector->toString().c_str());

    ControlGraph* const methodGraph = getControlGraph(method);
    assert(methodGraph);

    // TODO Handle recursive and tail calls
    TContextStack contextStack(*inferContext, parent);
    type::TypeAnalyzer analyzer(*this, *methodGraph, contextStack);
    analyzer.run();

    Type& returnType = inferContext->getReturnType();

    std::printf("%s::%s>>%s -> %s\n",
                arguments.toString().c_str(),
                method->klass->name->toString().c_str(),
                selector->toString().c_str(),
                returnType.toString().c_str());

    return inferContext;
}

InferContext* TypeSystem::inferBlock(Type& block, const Type& arguments, TContextStack* parent) {
    if (block.getKind() != Type::tkMonotype)
        return 0;

    TMethod* const method = block[Type::bstOrigin].getValue()->cast<TMethod>();
    const uint16_t offset = TInteger(block[Type::bstOffset].getValue());

    // TODO Cache
    InferContext* const inferContext = new InferContext(method, m_lastContextIndex++, arguments);

    ControlGraph* const methodGraph = getControlGraph(method);
    assert(methodGraph);

    std::printf("Analyzing block %s::%s ...\n", arguments.toString().c_str(), block.toString().c_str());

    st::ParsedMethod* const parsedMethod = methodGraph->getParsedMethod();
    st::ParsedBlock*  const parsedBlock  = parsedMethod->getParsedBlockByOffset(offset);

    // TODO Cache
    ControlGraph* const blockGraph = new ControlGraph(parsedMethod, parsedBlock);
    blockGraph->buildGraph();

    {
        std::ostringstream ss;
        ss << method->klass->name->toString() << ">>" << method->name->toString() << "@" << offset;

        ControlGraphVisualizer vis(blockGraph, ss.str(), "dots/");
        vis.run();
    }

    TContextStack contextStack(*inferContext, parent);
    type::TypeAnalyzer analyzer(*this, *blockGraph, contextStack);
    analyzer.run(&block);

    std::printf("%s::%s -> %s\n", arguments.toString().c_str(), block.toString().c_str(),
                inferContext->getReturnType().toString().c_str());

    return inferContext;
}
