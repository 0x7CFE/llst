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

template<> PushBlockNode* ControlNode::cast<PushBlockNode>() {
    if (this->getNodeType() != ntInstruction)
        return 0;

    InstructionNode* const node = static_cast<InstructionNode*>(this);
    if (node->getInstruction().getOpcode() != opcode::pushBlock)
        return 0;

    return static_cast<PushBlockNode*>(this);
}

template<> PushBlockNode* ControlGraph::newNode<PushBlockNode>() {
    PushBlockNode* node = new PushBlockNode(m_lastNodeIndex++);
    m_nodes.push_back(node);
    return static_cast<PushBlockNode*>(node);
}

class GraphConstructor : public InstructionVisitor {
public:
    GraphConstructor(ControlGraph* graph)
        : InstructionVisitor(graph->getParsedBytecode()), m_graph(graph) { }

    virtual bool visitBlock(BasicBlock& basicBlock) {
        m_currentDomain = m_graph->getDomainFor(&basicBlock);
        m_currentDomain->setBasicBlock(&basicBlock);

        std::printf("GraphConstructor::visitBlock : block %p (%.2u), domain %p\n", &basicBlock, basicBlock.getOffset(), m_currentDomain);
        return InstructionVisitor::visitBlock(basicBlock);
    }

    virtual bool visitInstruction(const TSmalltalkInstruction& instruction) {
        // Initializing instruction node
        InstructionNode* const newNode = createNode(instruction);
        newNode->setInstruction(instruction);
        newNode->setDomain(m_currentDomain);
        m_currentDomain->addNode(newNode);

        // Processing instruction by adding references
//         std::printf("GraphConstructor::visitInstruction : processing node %.2u %s%s \n",
//                     newNode->getIndex(),
//                     newNode->getInstruction().isBranch() ? "^" : "",
//                     newNode->getInstruction().isTerminator() ? "!" : ""
//                    );
        processNode(newNode);

        return true;
    }

private:
    InstructionNode* createNode(const TSmalltalkInstruction& instruction);
    void processNode(InstructionNode* node);
    void processSpecials(InstructionNode* node);
    void processPrimitives(InstructionNode* node);

    ControlGraph*  const m_graph;
    ControlDomain* m_currentDomain;
    bool m_skipStubInstructions;
};

InstructionNode* GraphConstructor::createNode(const TSmalltalkInstruction& instruction)
{
    if (instruction.getOpcode() == opcode::pushBlock)
        return m_graph->newNode<PushBlockNode>();
    else
        return m_graph->newNode<InstructionNode>();
}

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
            m_currentDomain->pushValue(node);
            break;

        case opcode::pushBlock: {
            const uint16_t blockEndOffset    = node->getInstruction().getExtra();
            ParsedMethod* const parsedMethod = m_graph->getParsedMethod();
            ParsedBlock*  const parsedBlock  = parsedMethod->getParsedBlockByEndOffset(blockEndOffset);

            node->cast<PushBlockNode>()->setParsedBlock(parsedBlock);
            m_currentDomain->pushValue(node);
        } break;

        case opcode::assignTemporary: // TODO Link with tau node
        case opcode::assignInstance:
            m_currentDomain->requestArgument(0, node, true);
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
            m_currentDomain->requestArgument(0, node);

        case special::selfReturn:
            assert(! m_currentDomain->getTerminator());
            m_currentDomain->setTerminator(node);
            break;

        case special::sendToSuper:
            m_currentDomain->requestArgument(0, node);
            m_currentDomain->pushValue(node);
            break;

        case special::duplicate:
            m_currentDomain->requestArgument(0, node, true);
            m_currentDomain->pushValue(node);
            break;

        case special::popTop:
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

    switch (instruction.getExtra()) {
        case primitive::blockInvoke:
            m_currentDomain->requestArgument(0, node); // block object
            for (uint32_t index = instruction.getArgument()-1; index > 0; index--) // FIXME
                m_currentDomain->requestArgument(index, node); // arguments
            break;

        default:
            if (instruction.getArgument() > 0) {
                for (int32_t index = instruction.getArgument()-1; index >= 0; index--)
                    m_currentDomain->requestArgument(index, node);
            }
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
    void processNode(ControlNode& node);
    void processBranching();
    void processArgumentRequests();
    void processRequest(ControlDomain* domain, std::size_t argumentIndex, const ControlDomain::TArgumentRequest& request);

    void mergePhi(PhiNode* source, PhiNode* target);
    ControlNode* getRequestedNode(ControlDomain* domain, std::size_t index);

    ControlDomain* m_currentDomain;
    ControlNode*   m_nodeToLink;
};

void GraphLinker::processNode(ControlNode& node)
{
    // In order to keep graph strongly connected, we link
    // node to the next one. This edge would be interpreted
    // as a control flow edge, not the stack value flow edge.

    // Linking pending node
    if (m_nodeToLink) {
//         std::printf("GraphLinker::processNode : linking nodes %.2u and %.2u\n", m_nodeToLink->getIndex(), node.getIndex());
        m_nodeToLink->addEdge(&node);
        m_nodeToLink = 0;
    }

    if (InstructionNode* const instruction = node.cast<InstructionNode>()) {
        if (instruction->getInstruction().isTerminator())
            return; // terminator nodes will take care of themselves
    }

    const TNodeSet& outEdges = node.getOutEdges();
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

void GraphLinker::processBranching()
{
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

void GraphLinker::processArgumentRequests()
{
    const ControlDomain::TRequestList& requestList = m_currentDomain->getRequestedArguments();
    for (std::size_t index = 0; index < requestList.size(); index++)
        processRequest(m_currentDomain, index, requestList[index]);
}

void GraphLinker::processRequest(ControlDomain* domain, std::size_t argumentIndex, const ControlDomain::TArgumentRequest& request)
{
    InstructionNode* const requestingNode = request.requestingNode;
    ControlNode*     const argument       = getRequestedNode(domain, argumentIndex);
    const ControlNode::TNodeType argumentType = argument->getNodeType();

    std::printf("GraphLinker::processNode : linking nodes of argument request %.2u and %.2u\n", argument->getIndex(), request.requestingNode->getIndex());

    requestingNode->setArgument(request.index, argument);
    argument->addConsumer(requestingNode);

    // We need to link the nodes only from the same domain
    // Cross domain references are handled separately
    if (argument->getDomain() == requestingNode->getDomain())
        argument->addEdge(requestingNode);

    if (argumentType == ControlNode::ntPhi) {
        argument->addEdge(requestingNode);

        // Registering phi as a consumer for all input nodes
        TNodeSet::iterator iNode = argument->getInEdges().begin();
        for (; iNode != argument->getInEdges().end(); ++iNode)
            (*iNode)->addConsumer(argument);
    }
}

void GraphLinker::mergePhi(PhiNode* source, PhiNode* target)
{
    // All incoming edges of source node became incoming edges of target node.
    TNodeSet::iterator iEdge = source->getInEdges().begin();
    while (iEdge != source->getInEdges().end()) {
        ControlNode* const argument = *iEdge++;

        argument->removeEdge(source);
        argument->addEdge(target);
    }

    // Deleting source node because it is no longer used
    m_graph->eraseNode(source);
}

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
            const std::size_t newIndex = argumentIndex - refererStackSize;
            assert(newIndex <= argumentIndex);
            ControlNode* const refererValue = getRequestedNode(refererDomain, newIndex);

            if (singleReferer) {
                result = refererValue;
            } else {
                // Nested phi nodes should be merged together
                if (PhiNode* const phi = refererValue->cast<PhiNode>())
                    mergePhi(phi, static_cast<PhiNode*>(result));
                else
                    refererValue->addEdge(result);
            }

        } else {
            const std::size_t  valueIndex = refererStackSize - 1 - argumentIndex;
            assert(valueIndex < refererStackSize);
            ControlNode* const stackValue = refererStack[valueIndex];

            if (singleReferer)
                result = stackValue;
            else
                stackValue->addEdge(result);
        }
    }

    assert(result);
    return result;
}

class GraphOptimizer : public PlainNodeVisitor {
public:
    GraphOptimizer(ControlGraph* graph) : PlainNodeVisitor(graph) {}

    virtual bool visitNode(ControlNode& node) {
        // If node pushes value on the stack but this value is not consumed
        // by another node, or the only consumer is a popTop instruction
        // then we may remove such node (or a node pair)

        if (InstructionNode* const instruction = node.cast<InstructionNode>()) {
            const TSmalltalkInstruction& nodeInstruction = instruction->getInstruction();
            if (!nodeInstruction.isTrivial() || !nodeInstruction.isValueProvider())
                return true;

            const TNodeSet& consumers = instruction->getConsumers();
            if (consumers.empty()) {
                std::printf("GraphOptimizer::visitNode : node %u is not consumed and may be removed\n", instruction->getIndex());
                m_nodesToRemove.push_back(instruction);
            } else if (consumers.size() == 1) {
                if (InstructionNode* const consumer = (*consumers.begin())->cast<InstructionNode>()) {
                    const TSmalltalkInstruction& consumerInstruction = consumer->getInstruction();
                    if (consumerInstruction == TSmalltalkInstruction(opcode::doSpecial, special::popTop)) {
                        std::printf("GraphOptimizer::visitNode : node %u is consumed only by popTop %u and may be removed\n",
                                    instruction->getIndex(),
                                    consumer->getIndex());

                        m_nodesToRemove.push_back(consumer);
                        m_nodesToRemove.push_back(instruction);
                    }
                }
            }
        } else if (PhiNode* const phi = node.cast<PhiNode>()) {
            if (phi->getInEdges().size() == 1) {
                std::printf("GraphOptimizer::visitNode : phi node %u has only one input and may be removed\n", phi->getIndex());
                m_nodesToRemove.push_back(phi);
            }
        }

        return true;
    }

    virtual void nodesVisited() {
        // Removing nodes that were optimized out
        TNodeList::iterator iNode = m_nodesToRemove.begin();
        for (; iNode != m_nodesToRemove.end(); ++iNode) {
            if (InstructionNode* const instruction = (*iNode)->cast<InstructionNode>())
                removeInstruction(instruction);
            else if (PhiNode* const phi = (*iNode)->cast<PhiNode>())
                removePhi(phi);
            else
                assert(false);
        }
    }

private:
    void removePhi(PhiNode* phi) {
        assert(phi->getInEdges().size() == 1);

        ControlNode* const valueSource = *phi->getInEdges().begin();
        InstructionNode* const valueTarget = (*phi->getOutEdges().begin())->cast<InstructionNode>();
        assert(valueTarget);

        std::printf("Skipping phi node %.2u and remapping edges to link values directly: %.2u <-- %.2u\n",
                    phi->getIndex(),
                    valueSource->getIndex(),
                    valueTarget->getIndex());

        valueSource->removeEdge(phi);
        phi->removeEdge(valueTarget);

        valueSource->removeConsumer(phi);

        valueSource->addConsumer(valueTarget);
        valueTarget->setArgument(phi->getPhiIndex(), valueSource);

        m_graph->eraseNode(phi);
    }

    void removeInstruction(InstructionNode* node) {
        // Trivial instructions should have only one outgoing edge
        assert(node->getOutEdges().size() == 1);

        ControlNode* const nextNode = * node->getOutEdges().begin();
        assert(nextNode && nextNode->getNodeType() == ControlNode::ntInstruction);

        // Fixing domain entry point
        ControlDomain* const domain = node->getDomain();
        if (domain->getEntryPoint() == node)
            domain->setEntryPoint(nextNode->cast<InstructionNode>());

        // Fixing incoming edges by remapping them to the next node
        TNodeSet::iterator iNode = node->getInEdges().begin();
        while (iNode != node->getInEdges().end()) {
            ControlNode* const sourceNode = *iNode++;

            std::printf("Remapping node %.2u from %.2u to %.2u\n",
                        sourceNode->getIndex(),
                        node->getIndex(),
                        nextNode->getIndex());

            sourceNode->removeEdge(node);
            sourceNode->addEdge(nextNode);
        }

        // Erasing outgoing edges
        iNode = node->getOutEdges().begin();
        while (iNode != node->getOutEdges().end())
        {
            ControlNode* const targetNode = *iNode++;

            std::printf("Erasing edge %.2u -> %.2u\n",
                        node->getIndex(),
                        targetNode->getIndex());

            node->removeEdge(targetNode);
        }

        // Removing node from the graph
        std::printf("Erasing node %.2u\n", node->getIndex());
        node->getDomain()->removeNode(node);
        m_graph->eraseNode(node);
    }

private:
    TNodeList m_nodesToRemove;
};

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

    // Optimizing graph by removing stalled nodes and merging linear branch sequences
    std::printf("Phase 3. Optimizing control graph\n");
    GraphOptimizer optimizer(this);
    optimizer.run();
}
