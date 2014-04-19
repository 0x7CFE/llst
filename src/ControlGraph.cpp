#include <analysis.h>

using namespace st;

template<> InstructionNode* ControlGraph::newNode<InstructionNode>() { return static_cast<InstructionNode*>(newNode(ControlNode::ntInstruction)); }
template<> PhiNode* ControlGraph::newNode<PhiNode>() { return static_cast<PhiNode*>(newNode(ControlNode::ntPhi)); }
template<> TauNode* ControlGraph::newNode<TauNode>() { return static_cast<TauNode*>(newNode(ControlNode::ntTau)); }

class GraphConstructor : public InstructionVisitor {
public:
    GraphConstructor(ControlGraph* graph) : InstructionVisitor(graph->getParsedMethod()), m_graph(graph) { }

    virtual bool visitBlock(BasicBlock& basicBlock) {
        m_currentDomain = m_graph->getDomainFor(&basicBlock);
        m_currentDomain->setBasicBlock(&basicBlock);
        return InstructionVisitor::visitBlock(basicBlock);
    }

    virtual bool visitInstruction(const TSmalltalkInstruction& instruction) {
        // Initializing instruction node
        InstructionNode* newNode = m_graph->newNode<InstructionNode>();
        newNode->setInstruction(instruction);
        newNode->setDomain(m_currentDomain);
        m_currentDomain->addNode(newNode);

        // Processing instruction by adding references
        processNode(newNode);

        return true;
    }

private:
    void processNode(InstructionNode* node);
    void processSpecials(InstructionNode* node);
    void processPrimitives(InstructionNode* node);

    ControlGraph*  m_graph;
    ControlDomain* m_currentDomain;
};

void GraphConstructor::processNode(InstructionNode* node)
{
    const TSmalltalkInstruction& instruction = node->getInstruction();

    if (! m_currentDomain->getEntryPoint())
        m_currentDomain->setEntryPoint(node);

    switch (instruction.getOpcode()) {
        case opcode::pushConstant:
        case opcode::pushLiteral:
        case opcode::pushArgument:
        case opcode::pushTemporary:   // TODO Link with tau node
        case opcode::pushInstance:
        case opcode::pushBlock:
            m_currentDomain->pushValue(node);
            break;

        case opcode::assignTemporary: // TODO Link with tau node
        case opcode::assignInstance:
        case opcode::sendUnary:
        case opcode::sendMessage:
            m_currentDomain->requestArgument(0, node);
            break;

        case opcode::sendBinary:
            m_currentDomain->requestArgument(1, node);
            m_currentDomain->requestArgument(0, node);
            break;

        case opcode::markArguments:
            for (uint32_t index = instruction.getArgument(); index > 0; )
                m_currentDomain->requestArgument(--index, node);
            break;

        case opcode::doSpecial:
            processSpecials(node);
            break;

        case opcode::doPrimitive:
            processPrimitives(node);
            break;

        default:
            ; // TODO
    }
}

void GraphConstructor::processSpecials(InstructionNode* node)
{
    const TSmalltalkInstruction& instruction = node->getInstruction();

    switch (instruction.getArgument()) {
        case special::stackReturn:
        case special::blockReturn:
        case special::sendToSuper:
            m_currentDomain->requestArgument(0, node);
            break;

        case special::duplicate:
            m_currentDomain->requestArgument(0, node);
            m_currentDomain->pushValue(node);
            break;

        case special::popTop:
            // m_currentDomain->popValue();
            m_currentDomain->requestArgument(0, node);
            break;

        case special::branchIfTrue:
        case special::branchIfFalse:
            m_currentDomain->requestArgument(0, node);
        case special::branch:
            assert(! m_currentDomain->getTerminator());
            m_currentDomain->setTerminator(node);
            break;
    }
}

void GraphConstructor::processPrimitives(InstructionNode* node)
{
    const TSmalltalkInstruction& instruction = node->getInstruction();

    switch (instruction.getArgument()) {
        case primitive::ioPutChar:
        case primitive::getClass:
        case primitive::getSize:
        case primitive::integerNew:
            m_currentDomain->requestArgument(0, node);
            break;

        case primitive::objectsAreEqual:
        case primitive::startNewProcess:
        case primitive::allocateObject:
        case primitive::allocateByteArray:
        case primitive::cloneByteObject:

        case primitive::arrayAt:
        case primitive::stringAt:

        case primitive::smallIntAdd:
        case primitive::smallIntDiv:
        case primitive::smallIntMod:
        case primitive::smallIntLess:
        case primitive::smallIntEqual:
        case primitive::smallIntMul:
        case primitive::smallIntSub:
        case primitive::smallIntBitOr:
        case primitive::smallIntBitAnd:
        case primitive::smallIntBitShift:

        case primitive::LLVMsendMessage:
            m_currentDomain->requestArgument(1, node);
            m_currentDomain->requestArgument(0, node);
            break;

        case primitive::arrayAtPut:
        case primitive::stringAtPut:
            m_currentDomain->requestArgument(2, node);
            m_currentDomain->requestArgument(1, node);
            m_currentDomain->requestArgument(0, node);
            break;

        case primitive::blockInvoke:
            m_currentDomain->requestArgument(0, node); // block object
            for (uint32_t index = instruction.getArgument() - 1; index > 0; index--) // FIXME
                m_currentDomain->requestArgument(index - 1, node); // arguments
            break;

        case primitive::bulkReplace:
            for (uint32_t index = 5; index > 0; )
                m_currentDomain->requestArgument(--index, node);
            break;

        default:
            ; //TODO
    }
}

class GraphLinker : public DomainVisitor {
public:
    GraphLinker(ControlGraph* graph) : DomainVisitor(graph) { }

    virtual bool visitDomain(ControlDomain& domain) {
        m_currentDomain = &domain;

        processBranching();
        processArgumentRequests();

        return true;
    }

private:
    void processBranching() {
        // Current domain's entry point should be linked to the terminators of referring domains
        InstructionNode* const entryPoint = m_currentDomain->getEntryPoint();
        assert(entryPoint);

        const BasicBlock::TBasicBlockSet& referers = m_currentDomain->getBasicBlock()->getReferers();
        BasicBlock::TBasicBlockSet::iterator iReferer = referers.begin();
        for (; iReferer != referers.end(); ++iReferer) {
            ControlDomain* const refererDomain = m_graph->getDomainFor(*iReferer);
            InstructionNode* const  terminator = refererDomain->getTerminator();
            assert(terminator && terminator->getInstruction().isBranch());

            terminator->addEdge(entryPoint);
        }
    }

    void processArgumentRequests() {
        const ControlDomain::TRequestList& requestList = m_currentDomain->getRequestedArguments();
        for (std::size_t index = 0; index < requestList.size(); index++)
            processRequest(index, requestList[index]);
    }

    void processRequest(std::size_t index, const ControlDomain::TArgumentRequest& request);

    ControlDomain* m_currentDomain;
};

void GraphLinker::processRequest(std::size_t index, const ControlDomain::TArgumentRequest& request)
{
    const BasicBlock::TBasicBlockSet& refererBlocks = m_currentDomain->getBasicBlock()->getReferers();

    // In case of exactly one referer we may link values directly
    const bool singleReferer = (refererBlocks.size() == 1);

    // Otherwise we should iterate through all referers and aggregate values using phi node
    PhiNode* const phiNode = singleReferer ? 0 : m_graph->newNode<PhiNode>();

    BasicBlock::TBasicBlockSet::iterator iBlock = refererBlocks.begin();
    for (; iBlock != refererBlocks.end(); ++iBlock) {
        ControlDomain* const refererDomain = m_graph->getDomainFor(* iBlock);
        const TNodeList&     refererStack  = refererDomain->getLocalStack();
        const std::size_t    refererStackSize = refererStack.size();

        if (index > refererStackSize - 1) {
            // TODO Referer block do not have enough values on it's stack.
            //      We need to go deeper and process it's referers in turn.
        } else {
            const std::size_t valueIndex = refererStackSize - 1 - index;
            ControlNode* value = refererStack[valueIndex];

            if (singleReferer)
                value->addEdge(request.requestingNode);
            else
                value->addEdge(phiNode);
        }
    }

    if (! singleReferer) {
        phiNode->setIndex(index);
        phiNode->addEdge(request.requestingNode);
        request.requestingNode->setArgument(request.index, phiNode);
    }
}

void ControlGraph::buildGraph()
{
    // Iterating through basic blocks of parsed method and constructing node domains
    GraphConstructor constructor(this);
    constructor.run();

    // Linking nodes that requested argument during previous stage.
    // They're linked using phi nodes or a direct link if possible.
    // Also branching edges are added so graph remains linked even if
    // no stack relations exist.
    GraphLinker linker(this);
    linker.run();
}
