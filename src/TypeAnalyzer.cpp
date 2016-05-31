#include <inference.h>
#include <visualization.h>

using namespace type;

static void printBlock(const Type& blockType, std::stringstream& stream) {
    if (blockType.getSubTypes().size() < 2) {
        stream << "(Block)";
        return;
    }

    TMethod* const method = blockType.getSubTypes()[0].getValue()->cast<TMethod>();
    const uint16_t offset = TInteger(blockType.getSubTypes()[1].getValue());

    // Class>>method@offset
    stream
        << method->klass->name->toString()
        << ">>" << method->name->toString()
        << "@" << offset;
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

void TypeAnalyzer::run(const Type* blockType /*= 0*/) {
    if (m_graph.isEmpty())
        return;

    m_blockType = blockType;

    // FIXME For correct inference we need to perform in-width traverse

    m_baseRun = true;
    m_literalBranch = true;
    m_walker.run(*m_graph.nodes_begin(), Walker::wdForward);

    std::cout << "Base run:" << std::endl;
    type::TTypeList::const_iterator iType = m_context.getTypeList().begin();
    for (; iType != m_context.getTypeList().end(); ++iType)
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
        std::cout << "Return path was inferred literally. No need to perform induction run." << std::endl;
        return;
    }

    if (m_graph.getMeta().hasBackEdgeTau) {
        m_baseRun = false;

        m_walker.resetStopNodes();
        m_walker.run(*m_graph.nodes_begin(), Walker::wdForward);

        std::cout << "Induction run:" << std::endl;
        type::TTypeList::const_iterator iType = m_context.getTypeList().begin();
        for (; iType != m_context.getTypeList().end(); ++iType)
            std::cout << iType->first << " " << iType->second.toString() << std::endl;

        if (returnType.getSubTypes().size() == 1)
            returnType = returnType[0];
        else
            returnType.setKind(Type::tkComposite);

        std::cout << "return type: " << m_context.getReturnType().toString() << std::endl;
    }
}

Type& TypeAnalyzer::getArgumentType(const InstructionNode& instruction, std::size_t index /*= 0*/) {
    ControlNode* const argNode = instruction.getArgument(index);
    Type& result = m_context[*argNode];

    if (PhiNode* const phi = argNode->cast<PhiNode>())
        result = processPhi(*phi);

    return result;
}

void TypeAnalyzer::processInstruction(const InstructionNode& instruction) {
//     std::printf("processing %.2u\n", instruction.getIndex());

    const TSmalltalkInstruction::TArgument argument = instruction.getInstruction().getArgument();

    switch (instruction.getInstruction().getOpcode()) {
        case opcode::pushArgument:
            m_context[instruction] = m_context.getArgument(argument);
            break;

        case opcode::pushConstant:    doPushConstant(instruction);    break;
        case opcode::pushLiteral:     doPushLiteral(instruction);     break;

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

void TypeAnalyzer::doSendBinary(const InstructionNode& instruction) {
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

    if (InferContext* const context = m_system.inferMessage(selector, arguments))
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

void TypeAnalyzer::doPushTemporary(const InstructionNode& instruction) {
    if (const TauNode* const tau = instruction.getTauNode()) {
        const Type& tauType = m_context[*tau];

        //if (tauType.getKind() == Type::tkUndefined)
        if (tau->getKind() == TauNode::tkAggregator)
            processTau(*tau);

        m_context[instruction] = tauType;
    } else if (m_blockType) {
        // Block invocation primitive pass block arguments through creating method's temporaries.
        // To simplify inference, we pass their types as context arguments.

        const uint16_t argIndex = TInteger(m_blockType->getSubTypes()[2].getValue());
        const TSmalltalkInstruction::TArgument tempIndex = instruction.getInstruction().getArgument();

        if (tempIndex >= argIndex)
            m_context[instruction] = m_context.getArgument(tempIndex - argIndex);
        else
            m_context[instruction] = Type(Type::tkPolytype);
    }
}

void TypeAnalyzer::doPushBlock(const InstructionNode& instruction) {
    if (const PushBlockNode* const pushBlock = instruction.cast<PushBlockNode>()) {
        TMethod* const origin = pushBlock->getParsedBlock()->getContainer()->getOrigin();
        const uint16_t offset = pushBlock->getParsedBlock()->getStartOffset();
        const uint16_t argIndex = instruction.getInstruction().getArgument();

        // Block[origin, offset]
        Type& blockType = m_context[instruction];

        blockType.set(globals.blockClass, Type::tkMonotype);
        blockType.pushSubType(origin);
        blockType.pushSubType(Type(TInteger(offset)));
        blockType.pushSubType(Type(TInteger(argIndex)));
    }
}

void TypeAnalyzer::doAssignTemporary(const InstructionNode& instruction) {
    if (const TauNode* const tau = instruction.getTauNode()) {
        if (tau->getKind() == TauNode::tkProvider) {
            m_context[*tau] = m_context[*instruction.getArgument()];
        }
    }
}

void TypeAnalyzer::doSendMessage(const InstructionNode& instruction) {
    TSymbolArray&  literals     = *m_graph.getParsedMethod()->getOrigin()->literals;
    const uint32_t literalIndex = instruction.getInstruction().getArgument();

    TSymbol* const selector     = literals[literalIndex];
    const Type&    arguments    = m_context[*instruction.getArgument()];

    Type& result = m_context[instruction];
    if (InferContext* const context = m_system.inferMessage(selector, arguments))
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
            const Type& block = m_context[*instruction.getArgument(0)];
            const Type& arg   = m_context[*instruction.getArgument(1)];

            Type arguments(Type::tkArray);
            arguments.pushSubType(arg);

            if (instruction.getArgumentsCount() == 3)
                arguments.pushSubType(m_context[*instruction.getArgument(2)]);

            if (InferContext* invokeContext = m_system.inferBlock(block, arguments))
                primitiveResult = invokeContext->getReturnType();
            else
                primitiveResult = Type(Type::tkPolytype);

            break;
        }

        case primitive::smallIntSub:
        case primitive::smallIntDiv:
        case primitive::smallIntMod:
        {
            const Type& self = m_context[*instruction.getArgument(0)];
            const Type& arg  = m_context[*instruction.getArgument(1)];

            if (isSmallInteger(self.getValue()) && isSmallInteger(arg.getValue())) {
                const int lhs = TInteger(self.getValue()).getValue();
                const int rhs = TInteger(arg.getValue()).getValue();

                switch (opcode) {
                    case primitive::smallIntSub: primitiveResult = Type(TInteger(lhs - rhs)); break;
                    case primitive::smallIntDiv: primitiveResult = Type(TInteger(lhs / rhs)); break;
                    case primitive::smallIntMod: primitiveResult = Type(TInteger(lhs % rhs)); break;
                }
            } else {
                // TODO Check for (SmallInt)
                primitiveResult = Type(globals.smallIntClass, Type::tkMonotype);
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

    {
        std::ostringstream ss;
        ss << method->klass->name->toString() + ">>" + method->name->toString();

        ControlGraphVisualizer vis(controlGraph, ss.str(), "dots/");
        vis.run();
    }

    return controlGraph;
}

InferContext* TypeSystem::inferMessage(TSelector selector, const Type& arguments) {
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

    InferContext* const inferContext = new InferContext(m_lastContextIndex++, arguments);
    contextMap[arguments] = inferContext;

    ControlGraph* const methodGraph = getControlGraph(method);
    assert(controlGraph);

    std::printf("Analyzing %s::%s>>%s...\n",
                arguments.toString().c_str(),
                method->klass->name->toString().c_str(),
                selector->toString().c_str());

    // TODO Handle recursive and tail calls
    type::TypeAnalyzer analyzer(*this, *methodGraph, *inferContext);
    analyzer.run();

    Type& returnType = inferContext->getReturnType();

    std::printf("%s::%s>>%s -> %s\n",
                arguments.toString().c_str(),
                method->klass->name->toString().c_str(),
                selector->toString().c_str(),
                returnType.toString().c_str());

    return inferContext;
}

InferContext* TypeSystem::inferBlock(const Type& block, const Type& arguments) {
    if (block.getKind() != Type::tkMonotype || arguments.getSubTypes().empty())
        return 0;

    // TODO Cache
    BlockInferContext* const inferContext = new BlockInferContext(m_lastContextIndex++, arguments);

    TMethod* const method = block.getSubTypes()[0].getValue()->cast<TMethod>();
    const uint16_t offset = TInteger(block.getSubTypes()[1].getValue());

    ControlGraph* const methodGraph = getControlGraph(method);
    assert(controlGraph);

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

    type::TypeAnalyzer analyzer(*this, *blockGraph, *inferContext);
    analyzer.run(&block);

    std::printf("%s::%s -> %s, ^%s\n", arguments.toString().c_str(), block.toString().c_str(),
                inferContext->getReturnType().toString().c_str(),
                inferContext->getBlockReturnType().toString().c_str());

    return inferContext;
}
