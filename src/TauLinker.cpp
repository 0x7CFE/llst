#include <cstdio>
#include <analysis.h>

using namespace st;

static const bool traces_enabled = false;

struct TAssignSite {
    InstructionNode* instruction;
    bool             byBackEdge;

    TAssignSite(InstructionNode* instruction, bool byBackEdge)
        : instruction(instruction), byBackEdge(byBackEdge) {}
};

typedef std::vector<TAssignSite> TAssignSiteList;

typedef std::pair<TauNode*, TauNode*> TTauPair;
typedef std::set<TTauPair> TTauPairSet;

typedef std::map<TauNode*, TTauPairSet> TRedundantTauMap;
TRedundantTauMap m_redundantTaus;

typedef std::set<TauNode*, NodeIndexCompare> TTauSet;
TTauSet m_processedTaus;

class AssignLocator : public GraphWalker {
public:
    AssignLocator(
        TSmalltalkInstruction::TArgument argument,
        const ControlGraph::TEdgeSet& backEdges,
        const TauLinker::TClosureMap& closures
    ) : argument(argument), backEdges(backEdges), closures(closures) {}

    virtual TVisitResult visitNode(st::ControlNode& node, const TPathNode* path) {
        // TODO Phi node

        InstructionNode* const instruction = node.cast<InstructionNode>();
        if (!instruction)
            return vrKeepWalking;

        //assert(instruction);

        switch (instruction->getInstruction().getOpcode()) {
            case opcode::assignTemporary:
                if (instruction->getInstruction().getArgument() == argument) {
                    const bool viaBackEdge = containsBackEdge(path);

                    if (traces_enabled) {
                        std::printf("Found assign site: Node %.2u, back edge: %s\n",
                                    instruction->getIndex(),
                                    viaBackEdge ? "yes" : "no");
                    }

                    assignSites.push_back(TAssignSite(instruction, viaBackEdge));
                    return vrSkipPath;
                }
                break;

            case opcode::sendBinary:
            case opcode::sendMessage: {
                TauLinker::TClosureMap::const_iterator iClosure = closures.find(instruction);
                if (iClosure == closures.end())
                    break;

                if (iClosure->second.writesIndex(argument)) {
                    const bool viaBackEdge = containsBackEdge(path);

                    if (traces_enabled) {
                        std::printf("Found assigning closure: Node %.2u, back edge: %s\n",
                                    instruction->getIndex(),
                                    viaBackEdge ? "yes" : "no");
                    }

                    assignSites.push_back(TAssignSite(instruction, viaBackEdge));
                    return vrSkipPath;
                }

                break;
            }

            default:
                break;
        }

            if (instruction->getInstruction().getOpcode() == opcode::assignTemporary) {
                if (instruction->getInstruction().getArgument() == argument) {
                    const bool viaBackEdge = containsBackEdge(path);

                    if (traces_enabled) {
                        std::printf("Found assign site: Node %.2u, back edge: %s\n",
                                    instruction->getIndex(),
                                    viaBackEdge ? "yes" : "no");
                    }

                    assignSites.push_back(TAssignSite(instruction, viaBackEdge));
                    return vrSkipPath;
                }
            }

        return vrKeepWalking;
    }

    bool containsBackEdge(const TPathNode* path) const {
        // Search for back edges in the located path

        for (const TPathNode* p = path; p->prev; p = p->prev) {
            const ControlGraph::TEdgeSet::const_iterator iEdge = backEdges.find(
                st::BackEdgeDetector::TEdge(
                    static_cast<const st::InstructionNode*>(p->node),
                    static_cast<const st::InstructionNode*>(p->prev->node)
                )
            );

            if (iEdge != backEdges.end())
                return true;
        }

        return false;
    }

    const TSmalltalkInstruction::TArgument argument;
    const ControlGraph::TEdgeSet& backEdges;
    const TauLinker::TClosureMap& closures;

    TAssignSiteList assignSites;
};

void TauLinker::addClosureNode(
    const st::InstructionNode& node,
    const ClosureTauNode::TIndexList& readIndices,
    const ClosureTauNode::TIndexList& writeIndices)
{
    TClosureInfo& closure = m_closures[&node];
    closure.readIndices   = readIndices;
    closure.writeIndices  = writeIndices;
}

st::GraphWalker::TVisitResult TauLinker::visitNode(st::ControlNode& node, const TPathNode* path) {
    st::BackEdgeDetector::visitNode(node, path);

    if (InstructionNode* const instruction = node.cast<InstructionNode>()) {
        switch (instruction->getInstruction().getOpcode()) {
            case opcode::pushTemporary:
                m_pendingNodes.insert(instruction);
                break;

            case opcode::assignTemporary:
                createType(*instruction);
                break;

            case opcode::sendBinary:
            case opcode::sendMessage:
                m_pendingNodes.insert(instruction);
                break;

            default:
                break;
        }
    }

    return st::GraphWalker::vrKeepWalking;
}

void TauLinker::nodesVisited() {
    // Detected back edges
    for (TEdgeSet::const_iterator iEdge = getBackEdges().begin(); iEdge != getBackEdges().end(); ++iEdge) {
        if (traces_enabled) {
            std::printf("Back edge: Node %.2u --> Node %.2u\n",
                        (*iEdge).from->getIndex(),
                        (*iEdge).to->getIndex()
            );
        }
    }

    getGraph().getMeta().hasLoops = !getBackEdges().empty();
    m_graph.getMeta().backEdges = getBackEdges();

    // When all nodes visited, process the pending list
    TInstructionSet::iterator iNode = m_pendingNodes.begin();
    for (; iNode != m_pendingNodes.end(); ++iNode) {
        InstructionNode& node = **iNode;

        switch (node.getInstruction().getOpcode()) {
            case opcode::pushTemporary:
                processPushTemporary(node);
                break;

            case opcode::sendBinary:
            case opcode::sendMessage:
                processClosure(node);
                break;

            default:
                break;
        }
    }

    optimizeTau();
}

void TauLinker::optimizeTau() {
    detectRedundantTau();
    eraseRedundantTau();
}

void TauLinker::eraseRedundantTau() {
    TRedundantTauMap::iterator iProvider = m_redundantTaus.begin();
    for (; iProvider != m_redundantTaus.end(); ++iProvider) {
        if (traces_enabled)
            printf("Now working on provider tau %.2u\n", (*iProvider).first->getIndex());

        TTauPairSet& pendingTaus = iProvider->second;
        TTauPairSet::iterator iPendingTau = pendingTaus.begin();

        iPendingTau = pendingTaus.begin();
        for ( ; iPendingTau != pendingTaus.end(); ++iPendingTau) {
            TauNode* const remainingTau = iPendingTau->first;
            TauNode* const redundantTau = iPendingTau->second;

            if (m_processedTaus.find(remainingTau) != m_processedTaus.end()) {
                if (traces_enabled)
                    printf("Tau %.2u was already processed earlier\n", remainingTau->getIndex());

                continue;
            }

            const TNodeSet& consumers = redundantTau->getConsumers();

            // Remap all consumers to the remainingTau
            TNodeSet::iterator iConsumer = consumers.begin();
            for ( ; iConsumer != consumers.end(); ++iConsumer) {
                // FIXME Could there be non-instruction nodes?
                if (InstructionNode* const instruction = (*iConsumer)->cast<InstructionNode>()) {
                    if (traces_enabled) {
                        printf("Remapping consumer %.2u from tau %.2u to remaining tau %.2u\n",
                               instruction->getIndex(),
                               redundantTau->getIndex(),
                               remainingTau->getIndex());
                    }

                    instruction->setTauNode(remainingTau);
                    remainingTau->addConsumer(instruction);
                }
            }

            // Remove all incomings of the redundantTau
            TauNode::TIncomingMap::const_iterator iIncoming = redundantTau->getIncomingMap().begin();
            for ( ; iIncoming != redundantTau->getIncomingMap().end(); ++iIncoming) {

                if (traces_enabled) {
                    printf("Redundant tau %.2u is no longer consumer of %.2u\n",
                           redundantTau->getIndex(),
                           iIncoming->first->getIndex());
                }

                iIncoming->first->removeConsumer(redundantTau);
            }

            // Marking tau as processed
            m_processedTaus.insert(redundantTau);

            if (traces_enabled)
                printf("Marking redundant tau %.2u as processed\n", redundantTau->getIndex());
        }
    }

    m_redundantTaus.clear();

    // Erasing all redundant taus completely
    TTauSet::const_iterator iProcessedTau = m_processedTaus.begin();
    for (; iProcessedTau != m_processedTaus.end(); ++iProcessedTau) {
        TauNode* const processedTau = *iProcessedTau;

        if (traces_enabled)
            printf("Erasing processed tau %.2u\n", processedTau->getIndex());

        assert(processedTau->getIncomingMap().empty());
        getGraph().eraseNode(processedTau);
    }

    m_processedTaus.clear();
}

void TauLinker::detectRedundantTau() {
    TTauList::const_iterator iProvider = m_providers.begin();
    for (; iProvider != m_providers.end(); ++iProvider) {
        const TNodeSet& consumers = (*iProvider)->getConsumers();
        if (consumers.size() < 2)
            continue;

        if (traces_enabled)
            printf("Looking for consumers of Tau %.2u (total %zu)\n", (*iProvider)->getIndex(), consumers.size());

        TNodeSet::iterator iConsumer1 = consumers.begin();
        for ( ; iConsumer1 != consumers.end(); ++iConsumer1) {
            TauNode* const tau1 = (*iConsumer1)->cast<TauNode>();
            if (! tau1 || tau1->getKind() == TauNode::tkClosure)
                continue;

            TNodeSet::iterator iConsumer2 = iConsumer1;
            ++iConsumer2;

            for (; iConsumer2 != consumers.end(); ++iConsumer2) {
                TauNode* const tau2 = (*iConsumer2)->cast<TauNode>();
                if (!tau2 || tau2->getKind() == TauNode::tkClosure)
                    continue;

                if (tau1->getIncomingMap() == tau2->getIncomingMap()) {
                    if (traces_enabled)
                        printf("Tau %.2u and %.2u may be optimized\n", tau1->getIndex(), tau2->getIndex());

                    m_redundantTaus[*iProvider].insert(std::make_pair(tau1, tau2));
                }
            }
        }
    }
}

void TauLinker::createType(InstructionNode& instruction) {
    if (instruction.getTauNode())
        return;

    TauNode* const tau = getGraph().newNode<TauNode>();
    tau->setKind(TauNode::tkProvider);
    tau->addIncoming(&instruction);
    instruction.setTauNode(tau);

    m_providers.push_back(tau);

    if (traces_enabled) {
        std::printf("New type: Node %u.%.2u --> Tau %.2u, type %d\n",
                    instruction.getDomain()->getBasicBlock()->getOffset(),
                    instruction.getIndex(),
                    tau->getIndex(),
                    tau->getKind()

        );
    }
}

void TauLinker::processPushTemporary(InstructionNode& instruction) {
    if (instruction.getTauNode())
        return;

    // Searching for all AssignTemporary's and closures that provide a value for current node
    AssignLocator locator(instruction.getInstruction().getArgument(), getBackEdges(), getClosures());
    locator.run(&instruction, GraphWalker::wdBackward, false);

    TauNode* aggregator = 0;
    if (locator.assignSites.size() > 1) {
        aggregator = m_graph.newNode<TauNode>();
        aggregator->setKind(TauNode::tkAggregator);
        aggregator->addConsumer(&instruction);
        instruction.setTauNode(aggregator);
    }

    TAssignSiteList::const_iterator iAssignSite = locator.assignSites.begin();
    for (; iAssignSite != locator.assignSites.end(); ++iAssignSite) {
        InstructionNode* const assignTemporary = (*iAssignSite).instruction->cast<InstructionNode>();
        assert(assignTemporary);

        TauNode* const assignType = assignTemporary->getTauNode();
        assert(assignType);

        if ((*iAssignSite).byBackEdge)
            getGraph().getMeta().hasBackEdgeTau = true;

        if (aggregator) {
            aggregator->addIncoming(assignType, (*iAssignSite).byBackEdge);
        } else {
            assignType->addConsumer(&instruction);
            instruction.setTauNode(assignType);
        }

        if (traces_enabled) {
            std::printf("Tau: Node %.2u --> Tau %.2u, assign site %.2u is %s\n",
                        instruction.getIndex(),
                        aggregator ? aggregator->getIndex() : assignType->getIndex(),
                        assignTemporary->getIndex(),
                        (*iAssignSite).byBackEdge ? "below" : "above"
            );
        }

    }
}

void TauLinker::processClosure(InstructionNode& instruction) {
    if (instruction.getTauNode())
        return;

    if (traces_enabled)
        std::printf("Analyzing closure %.2u\n", instruction.getIndex());

    TClosureMap::const_iterator iClosure = m_closures.find(&instruction);
    if (iClosure == m_closures.end())
        return;

    const TClosureInfo& closure = iClosure->second;

    if (!closure.readIndices.size() && !closure.writeIndices.size())
        return;

    ClosureTauNode* const closureTau = m_graph.newNode<ClosureTauNode>();
    closureTau->setOrigin(&instruction);
    closureTau->setKind(TauNode::tkClosure);
    closureTau->addConsumer(&instruction);
    instruction.setTauNode(closureTau);

    m_providers.push_back(closureTau);

    for (ClosureTauNode::TIndex index = 0; index < closure.readIndices.size(); index++) {
        // Searching for all AssignTemporary's that provide a value for current node
        AssignLocator locator(closure.readIndices[index], getBackEdges(), getClosures());
        locator.run(&instruction, GraphWalker::wdBackward, false);

        TauNode* aggregator = 0;
        if (locator.assignSites.size() > 1) {
            aggregator = m_graph.newNode<TauNode>();
            aggregator->setKind(TauNode::tkAggregator);
            closureTau->addIncoming(aggregator);
        }

        TAssignSiteList::const_iterator iAssignSite = locator.assignSites.begin();
        for (; iAssignSite != locator.assignSites.end(); ++iAssignSite) {
            InstructionNode* const assignNode = (*iAssignSite).instruction->cast<InstructionNode>();
            assert(assignNode);

            TauNode* const assignTau = assignNode->getTauNode();
            assert(assignTau);

            if ((*iAssignSite).byBackEdge)
                getGraph().getMeta().hasBackEdgeTau = true;

            if (aggregator)
                aggregator->addIncoming(assignTau, (*iAssignSite).byBackEdge);
            else
                closureTau->addIncoming(assignTau);

             if (traces_enabled) {
                std::printf("Tau %.2u <-- %s %.2u, assign site %.2u is %s\n",
                            assignTau->getIndex(),
                            aggregator ? "aggregator" : "closure",
                            aggregator ? aggregator->getIndex() : closureTau->getIndex(),
                            assignNode->getIndex(),
                            (*iAssignSite).byBackEdge ? "below" : "above"
                );
            }
        }
    }
}

void TauLinker::reset() {
    m_graph.eraseTauNodes();
    m_providers.clear();
    m_pendingNodes.clear();
    m_closures.clear();
    resetStopNodes();
}
