#include <cstdio>
#include <analysis.h>

using namespace st;

static const bool traces_enabled = false;

bool NodeIndexCompare::operator() (const ControlNode* a, const ControlNode* b) const
{
    return a->getIndex() < b->getIndex();
}

bool DomainOffsetCompare::operator() (const ControlDomain* a, const ControlDomain* b) const {
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
    PushBlockNode* const node = new PushBlockNode(m_lastNodeIndex++);
    m_nodes.push_back(node);
    return static_cast<PushBlockNode*>(node);
}

template<> BranchNode* ControlNode::cast<BranchNode>() {
    if (this->getNodeType() != ntInstruction)
        return 0;

    InstructionNode* const node = static_cast<InstructionNode*>(this);
    if (node->getInstruction().getOpcode() != opcode::doSpecial)
        return 0;

    switch (node->getInstruction().getArgument()) {
        case special::branch:
        case special::branchIfTrue:
        case special::branchIfFalse:
            return static_cast<BranchNode*>(this);
    }

    return 0;
}

template<> BranchNode* ControlGraph::newNode<BranchNode>() {
    BranchNode* const node = new BranchNode(m_lastNodeIndex++);
    m_nodes.push_back(node);
    return static_cast<BranchNode*>(node);
}

TNodeSet PhiNode::getRealValues() {
    TNodeSet values;

    for (std::size_t i = 0; i < m_incomingList.size(); i++) {
        if (PhiNode* const phi = m_incomingList[i].node->cast<PhiNode>()) {
            const TNodeSet& realValues = phi->getRealValues();
            values.insert(realValues.begin(), realValues.end());
        } else {
            values.insert(m_incomingList[i].node);
        }
    }

    return values;
}

class GraphConstructor : public InstructionVisitor {
public:
    GraphConstructor(ControlGraph* graph)
        : InstructionVisitor(graph->getParsedBytecode()), m_graph(graph) { }

    virtual bool visitBlock(BasicBlock& basicBlock) {
        m_currentDomain = m_graph->getDomainFor(&basicBlock);
        m_currentDomain->setBasicBlock(&basicBlock);

        if (traces_enabled)
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
        if (traces_enabled)
            std::printf("GraphConstructor::visitInstruction : processing node %.2u %s%s \n",
                newNode->getIndex(),
                newNode->getInstruction().isBranch() ? "^" : "",
                newNode->getInstruction().isTerminator() ? "!" : ""
            );

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
};

InstructionNode* GraphConstructor::createNode(const TSmalltalkInstruction& instruction)
{
    if (instruction.isBranch())
        return m_graph->newNode<BranchNode>();

    if (instruction.getOpcode() == opcode::pushBlock)
        return m_graph->newNode<PushBlockNode>();

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

        if (traces_enabled) {
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
        }

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
    ControlNode* optimizePhi(PhiNode* phi);

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
        if (traces_enabled)
            std::printf("GraphLinker::processNode : linking nodes %.2u and %.2u\n", m_nodeToLink->getIndex(), node.getIndex());

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
        ControlDomain* const refererDomain = getGraph().getDomainFor(*iReferer);
        BranchNode* const branch = refererDomain->getTerminator()->cast<BranchNode>();
        assert(branch);

        if (entryPoint->getDomain()->getBasicBlock()->getOffset() == branch->getInstruction().getExtra())
            branch->setTargetNode(entryPoint);
        else
            branch->setSkipNode(entryPoint);

        if (traces_enabled)
            std::printf("GraphLinker::processNode : linking nodes of referring graphs %.2u and %.2u\n", branch->getIndex(), entryPoint->getIndex());

        branch->addEdge(entryPoint);
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

    if (traces_enabled)
        std::printf("GraphLinker::processNode : linking nodes of argument request %.2u and %.2u\n", argument->getIndex(), request.requestingNode->getIndex());

    requestingNode->setArgument(request.index, argument);
    argument->addConsumer(requestingNode);

    // We need to link the nodes only from the same domain
    // Cross domain references are handled separately
    if (requestingNode->getNodeType() == ControlNode::ntPhi || argumentType == ControlNode::ntPhi || argument->getDomain() == requestingNode->getDomain())
        argument->addEdge(requestingNode);
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
    getGraph().eraseNode(source);
}

ControlNode* GraphLinker::optimizePhi(PhiNode* phi)
{
    TNodeSet incomingValues;

    const PhiNode::TIncomingList& incomings = phi->getIncomingList();
    assert(incomings.size() > 1); // Phi should have at least two incoming edges

    for (size_t index = 0; index < incomings.size(); index++)
        incomingValues.insert(incomings[index].node);

    assert(incomingValues.size());

    if (traces_enabled)
        std::printf("GraphLinker::optimizePhi : phi node %u has %u unique incoming values\n", phi->getIndex(), incomingValues.size());

    if (incomingValues.size() > 1)
        return phi; // Phi is ok, no need to optimize. Leave everything as is.

    // It seem that phi node is redundant becasue all of it's incomings link to the same value.
    // This may happen in a diamond-shaped reference diagram if incoming value stored in the top
    // domain and consumed in the bottom one. Left and right domains are not affected.
    // We may safely remove the phi because incoming value dominates it's consumer.

    if (traces_enabled)
        std::printf("GraphLinker::optimizePhi : phi node %u is redundant and may be removed\n", phi->getIndex());

    // This is the real value that should be returned
    ControlNode* const value = *incomingValues.begin();

    // Unlink and erase phi
    value->removeConsumer(phi);
    value->removeEdge(phi);
    getGraph().eraseNode(phi);

    return value;
}

ControlNode* GraphLinker::getRequestedNode(ControlDomain* domain, std::size_t argumentIndex)
{
    const BasicBlock::TBasicBlockSet& refererBlocks = domain->getBasicBlock()->getReferers();

    // In case of exactly one referer we may link values directly
    // Otherwise we should iterate through all referers and aggregate values using phi node
    const bool singleReferer = (refererBlocks.size() == 1);
    ControlNode* result = 0;

    if (!singleReferer) {
        PhiNode* const phi = getGraph().newNode<PhiNode>();
        phi->setDomain(domain);
        result = phi;
    }

    BasicBlock::TBasicBlockSet::iterator iBlock = refererBlocks.begin();
    for (; iBlock != refererBlocks.end(); ++iBlock) {
        ControlDomain* const refererDomain    = getGraph().getDomainFor(* iBlock);
        const TNodeList&     refererStack     = refererDomain->getLocalStack();
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
                result->cast<PhiNode>()->addIncoming(refererDomain, refererValue);
                refererValue->addConsumer(result);

                if (refererValue->getNodeType() == ControlNode::ntPhi)
                    refererValue->addEdge(result);
            }

        } else {
            const std::size_t  valueIndex = refererStackSize - 1 - argumentIndex;
            assert(valueIndex < refererStackSize);
            ControlNode* const stackValue = refererStack[valueIndex];

            if (singleReferer) {
                result = stackValue;
            } else {
                result->cast<PhiNode>()->addIncoming(refererDomain, stackValue);
                stackValue->addConsumer(result);

                if (stackValue->getNodeType() == ControlNode::ntPhi)
                    stackValue->addEdge(result);
            }
        }
    }

    if (!singleReferer)
        result = optimizePhi(result->cast<PhiNode>());

    assert(result);
    return result;
}

class GraphOptimizer : public PlainNodeVisitor {
public:
    GraphOptimizer(ControlGraph* graph) : PlainNodeVisitor(graph) {}

    bool graphAltered() const { return !m_nodesToRemove.empty(); }

private:
    virtual bool visitNode(ControlNode& node) {
//         if (BranchNode* const branch = node.cast<BranchNode>())
//             checkBranch(*branch);
//         else
        if (InstructionNode* const instruction = node.cast<InstructionNode>())
            checkInstruction(*instruction);

        return true;
    }

    virtual void nodesVisited() {
        // Removing nodes that were optimized out
        TNodeSet::const_iterator iNode = m_nodesToRemove.begin();
        for (; iNode != m_nodesToRemove.end(); ++iNode) {
            assert((*iNode)->getNodeType() == ControlNode::ntInstruction
                || (*iNode)->getNodeType() == ControlNode::ntPhi);

            if (InstructionNode* const instruction = (*iNode)->cast<InstructionNode>())
                removeInstruction(instruction);
            else if (PhiNode* const phi = (*iNode)->cast<PhiNode>())
                removePhi(phi);
        }
    }

private:
    void checkBranch(const BranchNode& branch) {
        // If branch is targets to an unconditional branch, latter may be removed

        if (BranchNode* const target_branch = branch.getTargetNode()->cast<BranchNode>()) {
            if (! target_branch->getSkipNode()) {
                if (traces_enabled)
                    std::printf("GraphOptimizer::visitNode : node %u is branch to unconditional branch %u and latter may be removed\n",
                                branch.getIndex(), target_branch->getIndex());

                m_nodesToRemove.insert(target_branch);
                return;
            }
        }

        if (!branch.getSkipNode())
            return;

        if (BranchNode* const target_branch = branch.getSkipNode()->cast<BranchNode>()) {
            if (! target_branch->getSkipNode()) {
                if (traces_enabled)
                    std::printf("GraphOptimizer::visitNode : node %u is branch to unconditional branch %u and latter may be removed\n",
                                branch.getIndex(), target_branch->getIndex());

                m_nodesToRemove.insert(target_branch);
                return;
            }
        }
    }

    void checkInstruction(InstructionNode& instruction) {
        // If node pushes value on the stack but this value is not consumed
        // by another node, or the only consumer is a popTop instruction
        // then we may remove such node (or a node pair)

        const TSmalltalkInstruction& nodeInstruction = instruction.getInstruction();
        if (!nodeInstruction.isTrivial() || !nodeInstruction.isValueProvider()) {
            if (! instruction.cast<PushBlockNode>())
                return;
        }

        const TNodeSet& consumers = instruction.getConsumers();
        if (consumers.empty()) {
            if (traces_enabled)
                std::printf("GraphOptimizer::visitNode : node %u is not consumed and may be removed\n", instruction.getIndex());

            m_nodesToRemove.insert(&instruction);
        } else if (consumers.size() == 1) {
            if (InstructionNode* const consumer = (*consumers.begin())->cast<InstructionNode>()) {
                const TSmalltalkInstruction& consumerInstruction = consumer->getInstruction();
                if (consumerInstruction == TSmalltalkInstruction(opcode::doSpecial, special::popTop)) {
                    if (traces_enabled)
                        std::printf("GraphOptimizer::visitNode : node %u is consumed only by popTop %u and may be removed\n",
                            instruction.getIndex(),
                            consumer->getIndex()
                        );

                    // TODO Remove phi incoming and probably remove phi node along with it's consumer
                    m_nodesToRemove.insert(consumer);
                    m_nodesToRemove.insert(&instruction);
                }
            }
        }
    }

    void removePhi(PhiNode* phi) {
        assert(phi->getInEdges().size() == 1);

        ControlNode* const valueSource = *phi->getInEdges().begin();
        InstructionNode* const valueTarget = (*phi->getOutEdges().begin())->cast<InstructionNode>();
        assert(valueTarget);

        if (traces_enabled)
            std::printf("Skipping phi node %.2u and remapping edges to link values directly: %.2u <-- %.2u\n",
                phi->getIndex(),
                valueSource->getIndex(),
                valueTarget->getIndex()
            );

        valueSource->removeEdge(phi);
        phi->removeEdge(valueTarget);

        valueSource->removeConsumer(phi);

        valueSource->addConsumer(valueTarget);
        valueTarget->setArgument(phi->getPhiIndex(), valueSource);

        m_graph->eraseNode(phi);
    }

    void removeInstruction(InstructionNode* node) {
        const TNodeSet& outEdges = node->getOutEdges();
        // Trivial instructions should have only one outgoing edge
        assert(outEdges.size() == 1);

        ControlNode* const nextNode = * outEdges.begin();
        assert(nextNode && nextNode->getNodeType() == ControlNode::ntInstruction);

        // Fixing domain entry point
        ControlDomain* const domain = node->getDomain();
        if (domain->getEntryPoint() == node)
            domain->setEntryPoint(nextNode->cast<InstructionNode>());

        // TODO Phi node

        // Fixing incoming edges by remapping them to the next node
        TNodeSet::const_iterator iNode = node->getInEdges().begin();
        while (iNode != node->getInEdges().end()) {
            ControlNode* const sourceNode = *iNode++;

            if (traces_enabled)
                std::printf("Remapping node %.2u from %.2u to %.2u\n",
                    sourceNode->getIndex(),
                    node->getIndex(),
                    nextNode->getIndex()
                );

            if (BranchNode* const branch = sourceNode->cast<BranchNode>()) {
                if (traces_enabled)
                    std::printf("Patching branch info %.2u -> %.2u\n",
                        sourceNode->getIndex(),
                        nextNode->getIndex()
                    );

                if (branch->getTargetNode() == node)
                    branch->setTargetNode(nextNode);
                else
                    branch->setSkipNode(nextNode);
            }

            sourceNode->removeEdge(node);
            sourceNode->addEdge(nextNode);
        }

        // Erasing outgoing edges
        iNode = node->getOutEdges().begin();
        while (iNode != node->getOutEdges().end())
        {
            ControlNode* const targetNode = *iNode++;

            if (traces_enabled)
                std::printf("Erasing edge %.2u -> %.2u\n", node->getIndex(), targetNode->getIndex());

            node->removeEdge(targetNode);
        }

        if (traces_enabled)
            std::printf("Erasing node %.2u\n", node->getIndex());

        // Removing node from the graph
        node->getDomain()->removeNode(node);
        m_graph->eraseNode(node);
    }

private:
    TNodeSet m_nodesToRemove;
};

class TauLinker : public NodeVisitor {
public:
    TauLinker(ControlGraph* graph) : NodeVisitor(graph) {}

private:
    typedef std::set<InstructionNode*, NodeIndexCompare> TInstructionSet;
    TInstructionSet m_pendingNodes;

    typedef std::list<TauNode*> TTauList;
    TTauList m_providers;

private:
    virtual bool visitNode(st::ControlNode& node) {
        if (InstructionNode* const instruction = node.cast<InstructionNode>()) {
            switch (instruction->getInstruction().getOpcode()) {
                case opcode::pushTemporary:
                    m_pendingNodes.insert(instruction);
                    break;

//                 case opcode::pushConstant:
//                 case opcode::pushLiteral:
                case opcode::assignTemporary:
                    createType(*instruction);
                    break;

//                 case opcode::pushArgument:
//                 case opcode::sendUnary:
//                 case opcode::sendBinary:
//                 case opcode::sendMessage:
//                     createType(*instruction);
//                     break;

                case opcode::doSpecial:
                    switch (instruction->getInstruction().getArgument()) {
//                         case special::duplicate:
//                             inheritType();
//                             break;

                        case special::sendToSuper:
                            createType(*instruction);
                            break;
                    }
                    break;

                default:
                    break;
            }
        }

        return true;
    }

    virtual void domainsVisited() {
        // When all nodes visited, process the pending list
        TInstructionSet::iterator iNode = m_pendingNodes.begin();
        for (; iNode != m_pendingNodes.end(); ++iNode)
            processPushTemporary(**iNode);

        optimizeTau();
    }

private:
    typedef std::pair<TauNode*, TauNode*> TTauPair;
    typedef std::set<TTauPair> TTauPairSet;

    typedef std::map<TauNode*, TTauPairSet> TRedundantTauMap;
    TRedundantTauMap m_redundantTaus;

    typedef std::set<TauNode*> TTauSet;
    TTauSet m_processedTaus;


private:
    void optimizeTau() {
        detectRedundantTau();
        eraseRedundantTau();
    }

    void eraseRedundantTau() {
        TRedundantTauMap::iterator iProvider = m_redundantTaus.begin();
        for (; iProvider != m_redundantTaus.end(); ++iProvider) {
            printf("Now working on provider tau %.2u\n", (*iProvider).first->getIndex());

            TTauPairSet& pendingTaus = iProvider->second;
            TTauPairSet::iterator iPendingTau = pendingTaus.begin();

            iPendingTau = pendingTaus.begin();
            for ( ; iPendingTau != pendingTaus.end(); ++iPendingTau) {
                TauNode* const remainingTau = iPendingTau->first;
                TauNode* const redundantTau = iPendingTau->second;

                if (m_processedTaus.find(remainingTau) != m_processedTaus.end()) {
                    printf("Tau %.2u was already processed earlier\n", remainingTau->getIndex());
                    continue;
                }

                const TNodeSet& consumers = redundantTau->getConsumers();

                // Remap all consumers to the remainingTau
                TNodeSet::iterator iConsumer = consumers.begin();
                for ( ; iConsumer != consumers.end(); ++iConsumer) {
                    // FIXME Could there be non-instruction nodes?
                    if (InstructionNode* const instruction = (*iConsumer)->cast<InstructionNode>()) {
                        printf("Remapping consumer %.2u from tau %.2u to remaining tau %.2u\n",
                            instruction->getIndex(),
                            redundantTau->getIndex(),
                            remainingTau->getIndex());

                        instruction->setTauNode(remainingTau);
                        remainingTau->addConsumer(instruction);
                    }
                }

                // Remove all incomings of the redundantTau
                TNodeSet::iterator iIncoming = redundantTau->getIncomingSet().begin();
                for ( ; iIncoming != redundantTau->getIncomingSet().end(); ++iIncoming) {
                    printf("Redundant tau %.2u is no longer consumer of %.2u\n",
                        redundantTau->getIndex(),
                        (*iIncoming)->getIndex());

                    (*iIncoming)->removeConsumer(redundantTau);
                }

                // Marking tau as processed
                m_processedTaus.insert(redundantTau);
                printf("Marking redundant tau %.2u as processed\n", redundantTau->getIndex());
            }
        }

        m_redundantTaus.clear();

        // Erasing all redundant taus completely
        TTauSet::const_iterator iProcessedTau = m_processedTaus.begin();
        for (; iProcessedTau != m_processedTaus.end(); ++iProcessedTau) {
            TauNode* const processedTau = *iProcessedTau;

            printf("Erasing processed tau %.2u\n", processedTau->getIndex());
            assert(processedTau->consumers.empty());
            getGraph().eraseNode(processedTau);
        }

        m_processedTaus.clear();
    }

    void detectRedundantTau() {
        TTauList::const_iterator iProvider = m_providers.begin();
        for (; iProvider != m_providers.end(); ++iProvider) {
            const TNodeSet& consumers = (*iProvider)->getConsumers();
            if (consumers.size() < 2)
                continue;

            printf("Looking for consumers of Tau %.2u (total %zu)\n",
                (*iProvider)->getIndex(), consumers.size());

            TNodeSet::iterator iConsumer1 = consumers.begin();
            for ( ; iConsumer1 != consumers.end(); ++iConsumer1) {
                TauNode* const tau1 = (*iConsumer1)->cast<TauNode>();
                if (! tau1)
                    continue;

                TNodeSet::iterator iConsumer2 = iConsumer1;
                ++iConsumer2;

                for (; iConsumer2 != consumers.end(); ++iConsumer2) {
                    TauNode* const tau2 = (*iConsumer2)->cast<TauNode>();
                    if (!tau2)
                        continue;

                    if (tau1->getIncomingSet() == tau2->getIncomingSet()) {
                        printf("Tau %.2u and %.2u may be optimized\n",
                            tau1->getIndex(), tau2->getIndex());

                        m_redundantTaus[*iProvider].insert(std::make_pair(tau1, tau2));
                    }
                }
            }
        }
    }

    void createType(InstructionNode& instruction) {
        TauNode* const tau = getGraph().newNode<TauNode>();
        tau->setKind(TauNode::tkProvider);
        tau->addIncoming(&instruction);
        instruction.setTauNode(tau);

        m_providers.push_back(tau);

        std::printf("New type: Node %u.%.2u --> Tau %.2u\n",
                    instruction.getDomain()->getBasicBlock()->getOffset(),
                    instruction.getIndex(),
                    tau->getIndex());
    }

    void processPushTemporary(InstructionNode& instruction) {
        // Searching for all AssignTemporary's that provide a value for current node
        AssignLocator locator(instruction.getInstruction().getArgument());
        locator.run(&instruction, GraphWalker::wdBackward);

        TInstructionSet::const_iterator iNode = locator.assignSites.begin();
        for (; iNode != locator.assignSites.end(); ++iNode) {
//             std::printf("Node %.2u is affected by node %.2u\n",
//                         instruction.getIndex(), (*iNode)->getIndex());

            InstructionNode* const assignTemporary = (*iNode)->cast<InstructionNode>();
            assert(assignTemporary);

            InstructionNode* const argument = assignTemporary->getArgument()->cast<InstructionNode>();
            if (!argument)
                continue;

            TauNode* const inheritedType =  assignTemporary->getTauNode(); //argument->getTauNode();
            assert(inheritedType);

            // FIXME Inherit type from argument

            if (! instruction.getTauNode()) {
                inheritedType->addConsumer(&instruction);
                instruction.setTauNode(inheritedType);

                std::printf("Inherit type: Tau %.2u <-- %.2u\n",
                            inheritedType->getIndex(),
                            instruction.getIndex());

            } else {
                TauNode* const current = instruction.getTauNode();

                if (current->getKind() == TauNode::tkProvider) {
                    current->removeConsumer(&instruction);

                    TauNode* const aggregator = getGraph().newNode<TauNode>();
                    aggregator->setKind(TauNode::tkAggregator);
                    aggregator->addIncoming(current);
                    aggregator->addIncoming(inheritedType);

                    aggregator->addConsumer(&instruction);
                    instruction.setTauNode(aggregator);

                    std::printf("Remapped tau: Node %.2u --> Tau %.2u to Tau %.2u\n",
                                instruction.getIndex(),
                                current->getIndex(),
                                aggregator->getIndex());

                } else {
                    current->addIncoming(inheritedType);

                    std::printf("Attached to existing tau: Node %.2u --> Tau %.2u\n",
                                instruction.getIndex(),
                                current->getIndex());
                }

//                 std::printf("Inherit type: Tau %.2u <-- %.2u\n",
//                             inheritedType->getIndex(),
//                             instruction.getIndex());
            }
        }
    }

    class AssignLocator : public GraphWalker {
    public:
        AssignLocator(TSmalltalkInstruction::TArgument argument) : argument(argument) {}

        virtual TVisitResult visitNode(st::ControlNode* node) {
            if (InstructionNode* const instruction = node->cast<InstructionNode>()) {
                if (instruction->getInstruction().getOpcode() == opcode::assignTemporary) {
                    if (instruction->getInstruction().getArgument() == argument) {
                        assignSites.insert(instruction);
                        return vrSkipPath;
                    }
                }
            }

            return vrKeepWalking;
        }

        const TSmalltalkInstruction::TArgument argument;
        TInstructionSet assignSites;
    };
};

void ControlGraph::buildGraph()
{
    if (traces_enabled)
        std::printf("Phase 1. Constructing control graph\n");

    // Iterating through basic blocks of parsed method and constructing node domains
    {
        GraphConstructor constructor(this);
        constructor.run();
    }

    if (traces_enabled)
        std::printf("Phase 2. Linking control graph\n");

    // Linking nodes that requested argument during previous stage.
    // They're linked using phi nodes or a direct link if possible.
    // Also branching edges are added so graph remains linked even if
    // no stack relations exist.
    {
        GraphLinker linker(this);
        linker.run();
    }

    if (traces_enabled)
        std::printf("Phase 3. Optimizing control graph\n");

    // Optimizing graph by removing stalled nodes and merging linear branch sequences
    {
        GraphOptimizer optimizer(this);
        optimizer.run();
    }

    // Linking PushTemporary and AssignTemporary pairs
    {
        TauLinker linker(this);
        linker.run();
    }

    {
        BackEdgeDetector detector;
        detector.run(*this);

        BackEdgeDetector::TEdgeList::const_iterator iEdge = detector.getBackEdges().begin();
        for (; iEdge != detector.getBackEdges().end(); ++iEdge) {
            const BackEdgeDetector::TEdge& edge = *iEdge;

            std::printf("Back edge %u.%.2u -> %u.%.2u\n",
                        edge.from->getDomain()->getBasicBlock()->getOffset(), edge.from->getIndex(),
                        edge.to->getDomain()->getBasicBlock()->getOffset(), edge.to->getIndex());
        }
    }
}
