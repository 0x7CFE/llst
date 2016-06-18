#ifndef LLST_ANALYSIS_INCLUDED
#define LLST_ANALYSIS_INCLUDED

#include <set>
#include <vector>
#include <algorithm>

#include <stapi.h>

namespace llvm {
    class Value;
    class PHINode;
}

namespace st {

// This detector scans method's nested blocks for block return instruction.
// Only block's instructions are traversed. Method's instructions are skipped.
//
// This pass is used to find out whether method code contains block return instruction.
// This instruction is handled in a very different way than the usual opcodes.
// Thus requires special handling. Block return is done by trowing an exception out of
// the block containing it. Then it's catched by the method's code to perform a return.
// In order not to bloat the code with unused try-catch code we're previously scanning
// the method's code to ensure that try-catch is really needed. If it is not, we simply
// skip its generation.
class BlockReturnDetector : public ParsedBlockVisitor {
public:
    BlockReturnDetector(ParsedMethod* parsedMethod) : ParsedBlockVisitor(parsedMethod), m_blockReturnFound(false) { }

    bool isBlockReturnFound() const { return m_blockReturnFound; }

protected:
    class InstructionDetector : public InstructionVisitor {
    public:
        InstructionDetector(ParsedBytecode* parsedBytecode) : InstructionVisitor(parsedBytecode), m_blockReturnFound(false) { }
        bool isBlockReturnFound() const { return m_blockReturnFound; }

    private:
        virtual bool visitInstruction(const TSmalltalkInstruction& instruction) {
            if (instruction.getOpcode() == opcode::doSpecial) {
               if (instruction.getArgument() == special::blockReturn) {
                   m_blockReturnFound = true;
                   return false;
               }
            }

            return true;
        }

        bool m_blockReturnFound;
    };

    virtual bool visitBlock(ParsedBlock& parsedBlock) {
        InstructionDetector detector(&parsedBlock);
        detector.run();

        if (detector.isBlockReturnFound()) {
            m_blockReturnFound = true;
            return false;
        }

        return true;
    }

private:
    bool m_blockReturnFound;
};

class ControlDomain;
class ControlNode;
class InstructionNode;

typedef std::vector<ControlNode*> TNodeList;

class NodeIndexCompare {
public:
    bool operator() (const ControlNode* a, const ControlNode* b) const;
};

class DomainOffsetCompare {
public:
    bool operator() (const ControlDomain* a, const ControlDomain* b) const;
};

typedef std::set<ControlNode*, NodeIndexCompare> TNodeSet;
typedef std::set<ControlDomain*, DomainOffsetCompare>  TDomainSet;

// ControlNode is a base class for elements of ControlGraph.
// Elements of a graph represent various types of relations
// of code, data and metainfo. Each node is linked to other
// nodes of a graph. Nodes within a graph are organized in
// groups which are called domains.
class ControlNode {
public:
    enum TNodeType {
        ntInstruction, // node representing VM instruction
        ntPhi,         // virtual node linking stack values from different blocks
        ntTau          // virtual node linkinig variable types from assignment sites
    };

    ControlNode(uint32_t index) : m_index(index), m_domain(0), m_value(0) { }
    virtual ~ControlNode() { }
    virtual TNodeType getNodeType() const = 0;

    // Dynamically cast node to a specified type.
    // If type does not match null is returned.
    template<class T> T* cast();
    template<class T> const T* cast() const;

    uint32_t getIndex() const { return m_index; }

    ControlDomain* getDomain() const { return m_domain; }
    void setDomain(ControlDomain* value) { m_domain = value; }

    const TNodeSet& getInEdges() const { return m_inEdges; }
    const TNodeSet& getOutEdges() const { return m_outEdges; }

    void addEdge(ControlNode* dest) {
        this->m_outEdges.insert(dest);
        dest->m_inEdges.insert(this);
    }

    void removeEdge(ControlNode* dest) {
        this->m_outEdges.erase(dest);
        dest->m_inEdges.erase(this);
    }

    void setValue(llvm::Value* value) { m_value = value; }
    llvm::Value* getValue() const { return m_value; }

    // Get a list of nodes which refer current node as argument and tau nodes
    void addConsumer(ControlNode* consumer) { m_consumers.insert(consumer); }
    void removeConsumer(ControlNode* consumer) { m_consumers.erase(consumer); }
    const TNodeSet& getConsumers() const { return m_consumers; }

private:
    uint32_t       m_index;
    TNodeType      m_type;
    TNodeSet       m_inEdges;
    TNodeSet       m_outEdges;
    ControlDomain* m_domain;

    llvm::Value*   m_value;
    TNodeSet       m_consumers;
};

class TauNode;

// Instruction node represents a signle VM instruction and it's relations in code.
class InstructionNode : public ControlNode {
public:
    InstructionNode(uint32_t index) : ControlNode(index), m_instruction(opcode::extended), m_tau(0) { }
    virtual TNodeType getNodeType() const { return ntInstruction; }

    void setInstruction(TSmalltalkInstruction instruction) { m_instruction = instruction; }
    const TSmalltalkInstruction& getInstruction() const { return m_instruction; }

    ControlNode* getArgument(const std::size_t index = 0) const {
        assert(index < m_arguments.size());
        return m_arguments[index];
    }

    void setArgument(const std::size_t index, ControlNode* value) {
        if (index >= m_arguments.size())
            m_arguments.resize(index + 1);
        m_arguments[index] = value;
    }

    std::size_t addArgument(ControlNode* value) {
        m_arguments.push_back(value);
        return m_arguments.size() - 1;
    }

    std::size_t getArgumentsCount() const { return m_arguments.size(); }

    typedef std::vector<ControlNode*> TArgumentList;
    typedef TArgumentList::iterator iterator;
    iterator begin() { return m_arguments.begin(); }
    iterator end() { return m_arguments.end(); }

    TauNode* getTauNode() const { return m_tau; }
    void setTauNode(TauNode* value) { m_tau = value; }

private:
    TSmalltalkInstruction m_instruction;
    TArgumentList         m_arguments;
    TauNode*              m_tau;
};

// PushBlockNode represents a single PushBlock instruction.
// It provides pointer to the associated ParsedBlock object.
// In all cases it may be safely treated as InstructionNode.
class PushBlockNode : public InstructionNode {
public:
    PushBlockNode(uint32_t index) : InstructionNode(index) {}

    void setParsedBlock(ParsedBlock* block) { m_parsedBlock = block; }
    ParsedBlock* getParsedBlock() const { return m_parsedBlock; }

private:
    ParsedBlock* m_parsedBlock;
};

class BranchNode : public InstructionNode {
public:
    BranchNode(uint32_t index) : InstructionNode(index), m_targetNode(0), m_skipNode(0) {}

    ControlNode* getTargetNode() const { return m_targetNode; }
    ControlNode* getSkipNode() const { return m_skipNode; }

    void setTargetNode(ControlNode* value) { m_targetNode = value; }
    void setSkipNode(ControlNode* value) { m_skipNode = value; }

private:
    ControlNode* m_targetNode;
    ControlNode* m_skipNode;
};

// Phi node act as a value aggregator from several domains.
// When value is pushed on the stack in one basic block and
// popped in another we say that actual values have a stack relation.
//
// The main purpose of it is to aggregate several pushes from basic blocks
// followed by a control branch to a signle pop block. When basic blocks
// gets converted to a domain of nodes such relation is encoded by inserting
// a phi node so that instruction that pops a value from the stack is converted
// to a node having a phi node as it's agrument.
class PhiNode : public ControlNode {
public:
    PhiNode(uint32_t index) : ControlNode(index), m_phiValue(0) { m_incomingList.reserve(2); }
    virtual TNodeType getNodeType() const { return ntPhi; }
    uint32_t getPhiIndex() const { return m_phiIndex; }
    void setPhiIndex(uint32_t value) { m_phiIndex = value; }

    struct TIncoming {
        ControlDomain* domain;
        ControlNode*   node;
    };

    typedef std::vector<TIncoming> TIncomingList;

    // Value node may or may not belong to the specified domain
    void addIncoming(ControlDomain* domain, ControlNode* value) {
        TIncoming incoming;
        incoming.domain = domain;
        incoming.node   = value;

        m_incomingList.push_back(incoming);
    }

    const TIncomingList& getIncomingList() const { return m_incomingList; }
    TNodeSet getRealValues() const;

    llvm::PHINode* getPhiValue() const { return m_phiValue; }
    void setPhiValue(llvm::PHINode* value) { m_phiValue = value; }

private:
    uint32_t       m_phiIndex;
    TIncomingList  m_incomingList;
    llvm::PHINode* m_phiValue;
};

// Tau node is reserved for further use in type inference subsystem.
// It will link variable type transitions across a method.
class TauNode : public ControlNode {

public:
    TauNode(uint32_t index) : ControlNode(index), m_kind(tkUnknown) { }
    virtual TNodeType getNodeType() const { return ntTau; }

    typedef std::map<ControlNode*, bool> TIncomingMap;

    void addIncoming(ControlNode* node, bool byBackEdge = false) {
        m_incomingMap[node] = byBackEdge;
        node->addConsumer(this);
    }

    const TIncomingMap& getIncomingMap() const { return m_incomingMap; }

    enum TKind {
        tkUnknown = 0,
        tkProvider,
        tkAggregator,
        tkClosure
    };

    void setKind(TKind value) { m_kind = value; }
    TKind getKind() const { return m_kind; }

private:
    TIncomingMap m_incomingMap;
    TKind m_kind;
};

class ClosureTauNode : public TauNode {
public:
    ClosureTauNode(uint32_t index) : TauNode(index), m_origin(0) { }

    typedef std::size_t TIndex;
    typedef std::vector<TIndex> TIndexList;

    InstructionNode* getOrigin() const { return m_origin; }
    void setOrigin(InstructionNode* node) { m_origin = node; }

private:
    InstructionNode* m_origin;
};

// Domain is a group of nodes within a graph
// that represent a single basic block
class ControlDomain {
public:
    typedef TNodeSet::iterator iterator;
    iterator begin() { return m_nodes.begin(); }
    iterator end() { return m_nodes.end(); }

    void addNode(ControlNode* node) { m_nodes.insert(node); }
    void removeNode(ControlNode* node) { m_nodes.erase(node); }

    InstructionNode* getEntryPoint() const { return m_entryPoint; }
    void setEntryPoint(InstructionNode* value) { m_entryPoint = value; }

    InstructionNode* getTerminator() const { return m_terminator; }
    void setTerminator(InstructionNode* value) { m_terminator = value; }

    BasicBlock* getBasicBlock() const { return m_basicBlock; }
    void setBasicBlock(BasicBlock* value) { m_basicBlock = value; }

    void pushValue(ControlNode* value) {
        m_localStack.push_back(value);
    }

    ControlNode* topValue(bool keep = false) {
        assert(! m_localStack.empty());

        ControlNode* const value = m_localStack.back();
        if (!keep)
            m_localStack.pop_back();

        return value;
    }

    void requestArgument(std::size_t index, InstructionNode* forNode, bool keep = false) {
        if (! m_localStack.empty()) {
            ControlNode* argument = topValue(keep);
            forNode->setArgument(index, argument);
            argument->addConsumer(forNode);

            if (argument->getNodeType() == ControlNode::ntPhi)
                argument->addEdge(forNode);

        } else {
            m_reqestedArguments.push_back(TArgumentRequest(index, forNode, keep));
        }
    }

    struct TArgumentRequest {
        std::size_t index;
        InstructionNode* requestingNode;
        bool keep;

        TArgumentRequest(std::size_t index, InstructionNode* requestingNode, bool keep)
            : index(index), requestingNode(requestingNode), keep(keep) {}
    };
    typedef std::vector<TArgumentRequest> TRequestList;

    const TRequestList& getRequestedArguments() const { return m_reqestedArguments; }
    const TNodeList& getLocalStack() const { return m_localStack; }

    ControlDomain(BasicBlock* basicBlock) : m_entryPoint(0), m_terminator(0), m_basicBlock(basicBlock) { }

private:
    TNodeSet         m_nodes;
    InstructionNode* m_entryPoint;
    InstructionNode* m_terminator;
    BasicBlock*      m_basicBlock;

    TNodeList        m_localStack;
    TRequestList     m_reqestedArguments;
};

class ControlGraph {
public:
    ControlGraph(ParsedMethod* parsedMethod)
        : m_parsedMethod(parsedMethod), m_parsedBlock(0), m_lastNodeIndex(0) { }

    ControlGraph(ParsedMethod* parsedMethod, ParsedBlock* parsedBlock)
        : m_parsedMethod(parsedMethod), m_parsedBlock(parsedBlock), m_lastNodeIndex(0)
    {
        m_metaInfo.isBlock = true;
    }

    ParsedMethod* getParsedMethod() const { return m_parsedMethod; }

    ParsedBytecode* getParsedBytecode() const {
        if (m_parsedBlock)
            return m_parsedBlock;
        else
            return m_parsedMethod;
    }

    typedef TDomainSet::iterator iterator;
    iterator begin() { return m_domains.begin(); }
    iterator end() { return m_domains.end(); }

    typedef std::list<ControlNode*> TNodeList;
    typedef TNodeList::iterator nodes_iterator;
    typedef TNodeList::reverse_iterator reverse_iterator;
    nodes_iterator nodes_begin() { return m_nodes.begin(); }
    nodes_iterator nodes_end() { return m_nodes.end(); }
    reverse_iterator nodes_rbegin() { return m_nodes.rbegin(); }
    reverse_iterator nodes_rend() { return m_nodes.rend(); }
    bool isEmpty() const { return m_nodes.begin() == m_nodes.end(); }

    ControlNode* newNode(ControlNode::TNodeType type) {
        assert(type == ControlNode::ntInstruction
            || type == ControlNode::ntPhi
            || type == ControlNode::ntTau
        );

        ControlNode* node = 0;

        switch (type) {
            case ControlNode::ntInstruction:
                node = new InstructionNode(m_lastNodeIndex);
                break;

            case ControlNode::ntPhi:
                node = new PhiNode(m_lastNodeIndex);
                break;

            case ControlNode::ntTau:
                node = new TauNode(m_lastNodeIndex);
                break;
        }

        m_lastNodeIndex++;

        m_nodes.push_back(node);
        return node;
    }

    template<class T> T* newNode();

    ControlDomain* newDomain(BasicBlock* basicBlock) {
        ControlDomain* const domain = new ControlDomain(basicBlock);
        m_domains.insert(domain);
        return domain;
    }

    void eraseNode(ControlNode* node) {
        // We allow to erase only orphan nodes
        assert(node);
        assert(!node->getInEdges().size());
        assert(!node->getOutEdges().size());

        m_nodes.remove(node);
        delete node;
    }

    void eraseTauNodes();

    ~ControlGraph() {
        TDomainSet::iterator iDomain = m_domains.begin();
        while (iDomain != m_domains.end())
            delete * iDomain++;

        TNodeList::iterator iNode = m_nodes.begin();
        while (iNode != m_nodes.end())
            delete * iNode++;
    }

    void buildGraph();

    ControlDomain* getDomainFor(BasicBlock* basicBlock) {
        TDomainMap::iterator iDomain = m_blocksToDomains.find(basicBlock);
        if (iDomain == m_blocksToDomains.end()) {
            ControlDomain* const domain = newDomain(basicBlock);
            m_blocksToDomains[basicBlock] = domain;
            return domain;
        }

        assert(iDomain->second);
        return iDomain->second;
    }

    struct TEdge {
        const InstructionNode* from;
        const InstructionNode* to;

        TEdge(const InstructionNode* from, const InstructionNode* to)
        : from(from), to(to)
        {
            assert(from);
            assert(to);
        }
    };

    class EdgeCompare {
    public:
        bool operator() (const TEdge& a, const TEdge& b) const {
            if (a.from < b.from)
                return true;

            if (a.from > b.from)
                return false;

            return a.to < b.to;
        }
    };

    typedef std::set<TEdge, EdgeCompare> TEdgeSet;

    struct TMetaInfo {
        bool isBlock;
        bool hasBlockReturn;
        bool hasLiteralBlocks;

        bool hasLoops;
        bool hasBackEdgeTau;

        bool usesSelf;
        bool usesSuper;

        bool readsArguments;
        bool readsFields;
        bool writesFields;

        bool hasPrimitive;

        TEdgeSet backEdges;

        typedef std::vector<std::size_t> TIndexList;
        TIndexList readsTemporaries;
        TIndexList writesTemporaries;

        static void insertIndex(std::size_t index, TIndexList& list) {
            if (std::find(list.begin(), list.end(), index) == list.end())
                list.push_back(index);
        }

        TMetaInfo();
    };

    TMetaInfo& getMeta() { return m_metaInfo; }

private:
    ParsedMethod* m_parsedMethod;
    ParsedBlock*  m_parsedBlock;
    TDomainSet    m_domains;

    TNodeList     m_nodes;
    uint32_t      m_lastNodeIndex;

    typedef std::map<BasicBlock*, ControlDomain*> TDomainMap;
    TDomainMap    m_blocksToDomains;

    TMetaInfo     m_metaInfo;
};

template<> InstructionNode* ControlNode::cast<InstructionNode>();
template<> PhiNode* ControlNode::cast<PhiNode>();
template<> TauNode* ControlNode::cast<TauNode>();

template<> InstructionNode* ControlGraph::newNode<InstructionNode>();
template<> PhiNode* ControlGraph::newNode<PhiNode>();
template<> TauNode* ControlGraph::newNode<TauNode>();

template<> PushBlockNode* ControlNode::cast<PushBlockNode>();
template<> PushBlockNode* ControlGraph::newNode<PushBlockNode>();
template<> const PushBlockNode* ControlNode::cast<PushBlockNode>() const;

template<> BranchNode* ControlNode::cast<BranchNode>();
template<> const BranchNode* ControlNode::cast<BranchNode>() const;
template<> BranchNode* ControlGraph::newNode<BranchNode>();

template<> ClosureTauNode* ControlNode::cast<ClosureTauNode>();
template<> ClosureTauNode* ControlGraph::newNode<ClosureTauNode>();
template<> const ClosureTauNode* ControlNode::cast<ClosureTauNode>() const;

class DomainVisitor {
public:
    DomainVisitor(ControlGraph* graph) : m_graph(graph) { }
    virtual ~DomainVisitor() { }

    virtual bool visitDomain(ControlDomain& /*domain*/) { return true; }
    virtual void domainsVisited() { }

    ControlGraph& getGraph() { return *m_graph; }

    void run() {
        ControlGraph::iterator iDomain = m_graph->begin();
        const ControlGraph::iterator iEnd = m_graph->end();

        if (iDomain != iEnd) {
            while (iDomain != iEnd) {
                if (! visitDomain(** iDomain))
                    break;

                ++iDomain;
            }

            domainsVisited();
        }
    }

private:
    ControlGraph* const m_graph;
};

class NodeVisitor : public DomainVisitor {
public:
    NodeVisitor(ControlGraph* graph) : DomainVisitor(graph) { }
    virtual bool visitNode(ControlNode& /*node*/) { return true; }
    virtual void nodesVisited() { }

protected:
    virtual bool visitDomain(ControlDomain& domain) {
        ControlDomain::iterator iNode = domain.begin();
        const ControlDomain::iterator iEnd = domain.end();

        if (iNode != iEnd) {
            while (iNode != iEnd) {
                if (! visitNode(** iNode))
                    return false;

                ++iNode;
            }

            nodesVisited();
        }

        return true;
    }
};

class PlainNodeVisitor {
public:
    PlainNodeVisitor(ControlGraph* graph) : m_graph(graph) { }
    virtual bool visitNode(ControlNode& /*node*/) { return true; }
    virtual void nodesVisited() { }

    void run() {
        ControlGraph::nodes_iterator iNode = m_graph->nodes_begin();
        const ControlGraph::nodes_iterator iEnd = m_graph->nodes_end();

        if (iNode != iEnd) {
            while (iNode != iEnd) {
                if (! visitNode(** iNode))
                    break;

                ++iNode;
            }

            nodesVisited();
        }
    }

protected:
    ControlGraph* const m_graph;
};

class GraphWalker {
public:
    GraphWalker() { }
    virtual ~GraphWalker() { }

    void resetStopNodes() { m_colorMap.clear(); }
    void addStopNode(ControlNode* node) { m_colorMap[node] = ncBlack; }

    void addStopNodes(const TNodeSet& nodes) {
        TNodeSet::const_iterator iNode = nodes.begin();
        for (; iNode != nodes.end(); ++iNode)
            m_colorMap[*iNode] = ncBlack;
    }

    enum TVisitResult {
        vrKeepWalking = 0,
        vrSkipPath,
        vrStopWalk
    };

    struct TPathNode {
        const ControlNode* const node;
        const TPathNode*   const prev;

        TPathNode(const ControlNode* node = 0, const TPathNode* prev = 0)
            : node(node), prev(prev) {}
    };

    virtual TVisitResult visitNode(ControlNode& node, const TPathNode* path) = 0;
    virtual void nodesVisited() { }

    enum TWalkDirection {
        wdForward,
        wdBackward
    };

    void run(ControlNode* startNode, TWalkDirection direction) {
        assert(startNode);
        m_direction = direction;

        TPathNode path(startNode);

        if (visitNode(*startNode, &path) != vrKeepWalking)
            return;

        walkIn(startNode, &path);
        nodesVisited();
    }

protected:
    enum TNodeColor {
        ncWhite = 0, // unvisited node
        ncGrey,      // node in progress
        ncBlack      // visited and settled node
    };

    typedef std::map<ControlNode*, TNodeColor> TColorMap;

    TNodeColor getNodeColor(ControlNode* node) const {
        TColorMap::const_iterator iColor = m_colorMap.find(node);
        if (iColor != m_colorMap.end())
            return iColor->second;
        else
            return ncWhite;
    }

private:
    bool walkIn(ControlNode* currentNode, const TPathNode* path) {
        m_colorMap[currentNode] = ncGrey;

        const TNodeSet& nodes = (m_direction == wdForward) ?
            currentNode->getOutEdges() : currentNode->getInEdges();

        for (TNodeSet::const_iterator iNode = nodes.begin(); iNode != nodes.end(); ++iNode) {
            ControlNode* const node = *iNode;

            if (getNodeColor(node) != ncWhite)
                continue;

            const TPathNode newPath(node, path);

            switch (const TVisitResult result = visitNode(*node, &newPath)) {
                case vrKeepWalking:
                    if (!walkIn(node, &newPath))
                        return false;
                    break;

                case vrStopWalk:
                    return false;

                case vrSkipPath:
                    continue;
            }
        }

        m_colorMap[currentNode] = ncBlack;
        return true;
    }

private:
    TWalkDirection m_direction;
    TColorMap      m_colorMap;
};

class PathVerifier : public GraphWalker {
public:
    PathVerifier(const TNodeSet& destinationNodes)
        : m_destinationNodes(destinationNodes), m_verified(false) {}

    bool isVerified() const { return m_verified; }
    void reset() { resetStopNodes(); m_verified = false; }

    void run(ControlNode* startNode) {
        assert(startNode);
        m_verified = false;

        GraphWalker::run(startNode, wdForward);
    }

private:
    virtual TVisitResult visitNode(ControlNode& node, const TPathNode*) {
        // Checking if there is a path between
        // start node and any of the destination nodes.

        if (m_destinationNodes.find(&node) != m_destinationNodes.end()) {
            m_verified = true;
            return vrStopWalk;
        }

        return vrKeepWalking;
    }

private:
    const TNodeSet& m_destinationNodes;
    bool m_verified;
};

class BackEdgeDetector : public GraphWalker {
public:
    typedef ControlGraph::TEdge TEdge;
    typedef ControlGraph::TEdgeSet TEdgeSet;

    const TEdgeSet& getBackEdges() const { return m_backEdges; }

    void run(ControlGraph& graph) {
        m_backEdges.clear();

        if (graph.nodes_begin() == graph.nodes_end())
            return;

        GraphWalker::run(*graph.nodes_begin(), GraphWalker::wdForward);
    }

protected:
    virtual TVisitResult visitNode(ControlNode& node, const TPathNode*) {
        if (BranchNode* const branch = node.cast<BranchNode>()) {
            InstructionNode* const target = branch->getTargetNode()->cast<InstructionNode>();
            assert(target);

            if (getNodeColor(target) == ncGrey)
                m_backEdges.insert(TEdge(branch, target));
        }

        return vrKeepWalking;
    }

private:
    TEdgeSet m_backEdges;
};

class TauLinker : private BackEdgeDetector {
public:
    TauLinker(ControlGraph& graph) : m_graph(graph) {}

    void run() { BackEdgeDetector::run(m_graph); }

    void addClosureNode(
        const InstructionNode& node,
        const ClosureTauNode::TIndexList& readIndices,
        const ClosureTauNode::TIndexList& writeIndices);

    struct TClosureInfo {
        ClosureTauNode::TIndexList readIndices;
        ClosureTauNode::TIndexList writeIndices;

        bool writesIndex(ClosureTauNode::TIndex index) const {
            return std::find(writeIndices.begin(), writeIndices.end(), index) != writeIndices.end();
        }
    };

    typedef std::map<const InstructionNode*, TClosureInfo> TClosureMap;
    const TClosureMap& getClosures() const { return m_closures; }

    void eraseTauNodes();
    void reset();

private:
    virtual st::GraphWalker::TVisitResult visitNode(st::ControlNode& node, const TPathNode* path);
    virtual void nodesVisited();

private:
    void optimizeTau();
    void eraseRedundantTau();
    void detectRedundantTau();

    void createType(InstructionNode& instruction);
    void processPushTemporary(InstructionNode& instruction);
    void processClosure(InstructionNode& instruction);

private:
    ControlGraph& m_graph;
    ControlGraph& getGraph() { return m_graph; }

    typedef std::set<InstructionNode*, NodeIndexCompare> TInstructionSet;
    TInstructionSet m_pendingNodes;

    typedef std::list<TauNode*> TTauList;
    TTauList m_providers;

    TClosureMap m_closures;
};

} // namespace st

#endif
