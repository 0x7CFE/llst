#ifndef LLST_ANALYSIS_INCLUDED
#define LLST_ANALYSIS_INCLUDED

#include <set>
#include <instructions.h>

namespace llvm {
    class Value;
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

typedef std::vector<ControlNode*> TNodeList;

class NodeIndexCompare {
public:
    bool operator() (const ControlNode* a, const ControlNode* b);
};

class DomainOffsetCompare {
public:
    bool operator() (const ControlDomain* a, const ControlDomain* b);
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

    uint32_t getIndex() const { return m_index; }

    ControlDomain* getDomain() const { return m_domain; }
    void setDomain(ControlDomain* value) { m_domain = value; }

    TNodeSet& getInEdges() { return m_inEdges; }
    TNodeSet& getOutEdges() { return m_outEdges; }

    void addEdge(ControlNode* to) {
        m_outEdges.insert(to);
        to->getInEdges().insert(this);
    }

    void removeEdge(ControlNode* to) {
        m_outEdges.erase(to);
        to->getInEdges().erase(this);
    }

    void setValue(llvm::Value* value) { m_value = value; }
    llvm::Value* getValue() const { return m_value; }

    // Get a list of nodes which refer current node as argument
    // Return true if at least one consumer is found
    bool getConsumers(TNodeList& result);

private:
    uint32_t       m_index;
    TNodeType      m_type;
    TNodeSet       m_inEdges;
    TNodeSet       m_outEdges;
    ControlDomain* m_domain;

    llvm::Value*   m_value;
};

// Instruction node represents a signle VM instruction and it's relations in code.
class InstructionNode : public ControlNode {
public:
    InstructionNode(uint32_t index) : ControlNode(index), m_instruction(opcode::extended) { }
    virtual TNodeType getNodeType() const { return ntInstruction; }

    void setInstruction(TSmalltalkInstruction instruction) { m_instruction = instruction; }
    const TSmalltalkInstruction& getInstruction() const { return m_instruction; }

    ControlNode* getArgument(const std::size_t index = 0) const {
        assert(index >= 0 && index < m_arguments.size());
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

private:
    TSmalltalkInstruction m_instruction;
    TArgumentList         m_arguments;
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
    PhiNode(uint32_t index) : ControlNode(index) { }
    virtual TNodeType getNodeType() const { return ntPhi; }
    uint32_t getPhiIndex() const { return m_phiIndex; }
    void setPhiIndex(uint32_t value) { m_phiIndex = value; }

private:
    uint32_t m_phiIndex;
};

// Tau node is reserved for further use in type inference subsystem.
// It will link variable type transitions across a method.
class TauNode : public ControlNode {
public:
    TauNode(uint32_t index) : ControlNode(index) { }
    virtual TNodeType getNodeType() const { return ntTau; }
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

    ControlNode* popValue() {
        assert(! m_localStack.empty());

        ControlNode* value = m_localStack.back();
        m_localStack.pop_back();
        return value;
    }

    void requestArgument(std::size_t index, InstructionNode* forNode) {
        if (! m_localStack.empty()) {
            ControlNode* argument = popValue();
            argument->addEdge(forNode);
            forNode->setArgument(index, argument);
        } else {
            m_reqestedArguments.push_back((TArgumentRequest){index, forNode});
        }
    }

    struct TArgumentRequest {
        std::size_t index;
        InstructionNode* requestingNode;
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
        : m_parsedMethod(parsedMethod), m_parsedBlock(0), m_lastNodeIndex(0)  { }

    ControlGraph(ParsedMethod* parsedMethod, ParsedBlock* parsedBlock)
        : m_parsedMethod(parsedMethod), m_parsedBlock(parsedBlock), m_lastNodeIndex(0) { }

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

    ControlNode* newNode(ControlNode::TNodeType type) {
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

            default:
                assert(false);
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

private:
    ParsedMethod* m_parsedMethod;
    ParsedBlock*  m_parsedBlock;
    TDomainSet    m_domains;

    typedef std::list<ControlNode*> TNodeList;
    TNodeList     m_nodes;
    uint32_t      m_lastNodeIndex;

    typedef std::map<BasicBlock*, ControlDomain*> TDomainMap;
    TDomainMap m_blocksToDomains;
};

template<> InstructionNode* ControlNode::cast<InstructionNode>();
template<> PhiNode* ControlNode::cast<PhiNode>();
template<> TauNode* ControlNode::cast<TauNode>();

template<> InstructionNode* ControlGraph::newNode<InstructionNode>();
template<> PhiNode* ControlGraph::newNode<PhiNode>();
template<> TauNode* ControlGraph::newNode<TauNode>();

template<> PushBlockNode* ControlNode::cast<PushBlockNode>();
template<> PushBlockNode* ControlGraph::newNode<PushBlockNode>();

class DomainVisitor {
public:
    DomainVisitor(ControlGraph* graph) : m_graph(graph) { }
    virtual ~DomainVisitor() { }

    virtual bool visitDomain(ControlDomain& domain) { return true; }
    virtual void domainsVisited() { }

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

protected:
    ControlGraph* const m_graph;
};

class NodeVisitor : public DomainVisitor {
public:
    NodeVisitor(ControlGraph* graph) : DomainVisitor(graph) { }
    virtual bool visitNode(ControlNode& node) { return true; }
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

} // namespace st

#endif
