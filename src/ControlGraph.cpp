#include <cstdio>
#include <analysis.h>

using namespace st;

bool NodeIndexCompare::operator() (const ControlNode* a, const ControlNode* b)
{
    return a->getIndex() < b->getIndex();
}

bool DomainOffsetCompare::operator() (const ControlDomain* a, const ControlDomain* b) {
    return a->getBasicBlock()->getOffset() < b->getBasicBlock()->getOffset();
}

template<> InstructionNode* ControlNode::cast<InstructionNode>() { return this->getNodeType() == ntInstruction ? static_cast<InstructionNode*>(this) : 0; }
template<> PhiNode* ControlNode::cast<PhiNode>() { return this->getNodeType() == ntPhi ? static_cast<PhiNode*>(this) : 0; }
template<> TauNode* ControlNode::cast<TauNode>() { return this->getNodeType() == ntTau ? static_cast<TauNode*>(this) : 0; }

template<> InstructionNode* ControlGraph::newNode<InstructionNode>() { return static_cast<InstructionNode*>(newNode(ControlNode::ntInstruction)); }
template<> PhiNode* ControlGraph::newNode<PhiNode>() { return static_cast<PhiNode*>(newNode(ControlNode::ntPhi)); }
template<> TauNode* ControlGraph::newNode<TauNode>() { return static_cast<TauNode*>(newNode(ControlNode::ntTau)); }

class GraphConstructor : public InstructionVisitor {
public:
    GraphConstructor(ControlGraph* graph)
        : InstructionVisitor(graph->getParsedMethod()), m_graph(graph) { }

    virtual bool visitBlock(BasicBlock& basicBlock) {
        m_currentDomain = m_graph->getDomainFor(&basicBlock);
        m_currentDomain->setBasicBlock(&basicBlock);

        std::printf("GraphConstructor::visitBlock : block %p (%.2u), domain %p\n", &basicBlock, basicBlock.getOffset(), m_currentDomain);
        return InstructionVisitor::visitBlock(basicBlock);
    }

    virtual bool visitInstruction(const TSmalltalkInstruction& instruction) {
        // Initializing instruction node
        InstructionNode* newNode = m_graph->newNode<InstructionNode>();
        newNode->setInstruction(instruction);
        newNode->setDomain(m_currentDomain);
        m_currentDomain->addNode(newNode);

        // Processing instruction by adding references
        std::printf("GraphConstructor::visitInstruction : processing node %.2u %s%s \n",
                    newNode->getIndex(),
                    newNode->getInstruction().isBranch() ? "^" : "",
                    newNode->getInstruction().isTerminator() ? "!" : ""
                   );
        processNode(newNode);

        return true;
    }

private:
    void processNode(InstructionNode* node);
    void processSpecials(InstructionNode* node);
    void processPrimitives(InstructionNode* node);

    ControlGraph*  m_graph;
    ControlDomain* m_currentDomain;
    bool m_skipStubInstructions;
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
            // FIXME should not pop/push the stack
            m_currentDomain->requestArgument(0, node);
            m_currentDomain->pushValue(node);
            break;

        case opcode::sendUnary:
        case opcode::sendMessage:
            m_currentDomain->requestArgument(0, node);
            m_currentDomain->pushValue(node);
            break;

        case opcode::sendBinary:
            m_currentDomain->requestArgument(1, node);
            m_currentDomain->requestArgument(0, node);
            m_currentDomain->pushValue(node);
            break;

        case opcode::markArguments:
            for (uint32_t index = instruction.getArgument(); index > 0; )
                m_currentDomain->requestArgument(--index, node);
            m_currentDomain->pushValue(node);
            break;

        case opcode::doSpecial:
            processSpecials(node);
            break;

        case opcode::doPrimitive:
            processPrimitives(node);
            m_currentDomain->pushValue(node);
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

            assert(! m_currentDomain->getTerminator());
            m_currentDomain->setTerminator(node);
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

class GraphLinker : public NodeVisitor {
public:
    GraphLinker(ControlGraph* graph) : NodeVisitor(graph), m_currentDomain(0), m_nodeToLink(0) { }

    virtual bool visitDomain(ControlDomain& domain) {
        m_currentDomain = &domain;

        std::printf("GraphLinker::visitDomain : processing domain %p, block offset %.2u, referrers %u, local stack %u, requested args %.2u\n",
                    &domain,
                    domain.getBasicBlock()->getOffset(),
                    domain.getBasicBlock()->getReferers().size(),
                    domain.getLocalStack().size(),
                    domain.getRequestedArguments().size()
                   );

        for (std::size_t index = 0; index < domain.getRequestedArguments().size(); index++)
            std::printf("GraphLinker::visitDomain : arg request %u, node index %.2u\n",
                        index, domain.getRequestedArguments()[index].requestingNode->getIndex());

        processBranching();
        processArgumentRequests();

        return NodeVisitor::visitDomain(domain);
    }

    virtual bool visitNode(ControlNode& node) {
        // Some nodes within a domain are not linked by out edges
        // to the rest of the domain nodes with higher indices.

        processNode(node);
        return true;
    }

private:
    void processNode(ControlNode& node) {
        // In order to keep graph strongly connected, we link
        // node to the next one. This edge would be interpreted
        // as a control flow edge, not the stack value flow edge.

        // Linking pending node
        if (m_nodeToLink) {
            std::printf("GraphLinker::processNode : fallback linking nodes %.2u and %.2u\n", m_nodeToLink->getIndex(), node.getIndex());
            m_nodeToLink->addEdge(&node);
            m_nodeToLink = 0;
        }

        InstructionNode* instruction = node.cast<InstructionNode>();
        if (instruction && instruction->getInstruction().isTerminator())
            return; // terminator nodes will take care of themselves

        TNodeSet& outEdges = node.getOutEdges();
        TNodeSet::iterator iNode = outEdges.begin();
        bool isNodeLinked = false;
        for (; iNode != outEdges.end(); ++iNode) {
            // Checking for connectivity
            if ((*iNode)->getDomain() == node.getDomain() && (*iNode)->getIndex() > node.getIndex()) {
                // Node is linked. No need to worry.
                isNodeLinked = true;
                break;
            }
        }

        if (! isNodeLinked)
            m_nodeToLink = &node;
    }

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

            std::printf("GraphLinker::processNode : linking nodes of referring graphs %.2u and %.2u\n", terminator->getIndex(), entryPoint->getIndex());
            terminator->addEdge(entryPoint);
        }
    }

    void processArgumentRequests() {
        const ControlDomain::TRequestList& requestList = m_currentDomain->getRequestedArguments();
        for (std::size_t index = 0; index < requestList.size(); index++)
            processRequest(m_currentDomain, index, requestList[index]);
    }

    void processRequest(ControlDomain* domain, std::size_t argumentIndex, const ControlDomain::TArgumentRequest& request) {
        ControlNode* node = getRequestedNode(domain, argumentIndex);

        std::printf("GraphLinker::processNode : linking nodes of argument request %.2u and %.2u\n", node->getIndex(), request.requestingNode->getIndex());
        node->addEdge(request.requestingNode);
        request.requestingNode->setArgument(request.index, node);
    }

    ControlNode* getRequestedNode(ControlDomain* domain, std::size_t index);

    ControlDomain* m_currentDomain;
    ControlNode*   m_nodeToLink;
};

ControlNode* GraphLinker::getRequestedNode(ControlDomain* domain, std::size_t argumentIndex)
{
    const BasicBlock::TBasicBlockSet& refererBlocks = domain->getBasicBlock()->getReferers();

    // In case of exactly one referer we may link values directly
    // Otherwise we should iterate through all referers and aggregate values using phi node
    const bool singleReferer = (refererBlocks.size() == 1);
    ControlNode* result = singleReferer ? 0 : m_graph->newNode<PhiNode>();

    BasicBlock::TBasicBlockSet::iterator iBlock = refererBlocks.begin();
    for (; iBlock != refererBlocks.end(); ++iBlock) {
        ControlDomain* const refererDomain = m_graph->getDomainFor(* iBlock);
        const TNodeList&     refererStack  = refererDomain->getLocalStack();
        const std::size_t    refererStackSize = refererStack.size();

        if (!refererStackSize || argumentIndex > refererStackSize - 1) {
            // Referer block do not have enough values on it's stack.
            // We need to go deeper and process it's referers in turn.
            ControlNode* refererValue = getRequestedNode(refererDomain, argumentIndex);

            if (singleReferer)
                result = refererValue;
            else
                refererValue->addEdge(result);

        } else {
            const std::size_t valueIndex = refererStackSize - 1 - argumentIndex;
            ControlNode* stackValue = refererStack[valueIndex];

            if (singleReferer)
                result = stackValue;
            else
                stackValue->addEdge(result);
        }
    }

    assert(result);
    return result;
}

void ControlGraph::buildGraph()
{
    // Iterating through basic blocks of parsed method and constructing node domains
    std::printf("Phase 1. Constructing control graph\n");
    GraphConstructor constructor(this);
    constructor.run();

    // Linking nodes that requested argument during previous stage.
    // They're linked using phi nodes or a direct link if possible.
    // Also branching edges are added so graph remains linked even if
    // no stack relations exist.
    std::printf("Phase 2. Linking control graph\n");
    GraphLinker linker(this);
    linker.run();
}
