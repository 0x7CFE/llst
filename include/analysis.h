#ifndef LLST_ANALYSIS_INCLUDED
#define LLST_ANALYSIS_INCLUDED

#include <set>

#include <instructions.h>

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
typedef std::set<ControlNode*>   TNodeSet;
typedef std::set<ControlDomain*> TDomainSet;

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

    virtual ~ControlNode() { }
    virtual TNodeType getNodeType() const = 0;

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
private:
    TNodeType      m_type;
    TNodeSet       m_inEdges;
    TNodeSet       m_outEdges;

    ControlDomain* m_domain;
};

// Instruction node represents a signle VM instruction and it's relations in code.
class InstructionNode : public ControlNode {
public:
    InstructionNode() : m_instruction(opcode::extended) { }
    virtual TNodeType getNodeType() const { return ntInstruction; }

    void setInstruction(TSmalltalkInstruction instruction) { m_instruction = instruction; }
    TSmalltalkInstruction getInstruction() const { return m_instruction; }

    ControlNode* getArgument(const std::size_t index) const {
        assert(index >= 0 && index < m_arguments.size());
        return m_arguments[index];
    }

    void setArgument(const std::size_t index, ControlNode* value) {
        if (index >= m_arguments.size())
            m_arguments.resize(index + 1);
        m_arguments[index] = value;
    }

    uint32_t addArgument(ControlNode* value) {
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
    virtual TNodeType getNodeType() const { return ntPhi; }
    uint32_t getIndex() const { return m_index; }
    void setIndex(uint32_t value) { m_index = value; }

private:
    uint32_t m_index;
};

// Tau node is reserved for further use in type inference subsystem.
// It will link variable type transitions across a method.
class TauNode : public ControlNode {
public:
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

    BasicBlock* getBasicBlock() const { return m_basicBlock; }
    void setBasicBlock(BasicBlock* value) { m_basicBlock = value; }

private:
    TNodeSet    m_nodes;
    BasicBlock* m_basicBlock;
};

class ControlGraph {
public:
    ControlGraph(ParsedMethod* parsedMethod) : m_parsedMethod(parsedMethod) { }

    typedef TDomainSet::iterator iterator;
    iterator begin() { return m_domains.begin(); }
    iterator end() { return m_domains.end(); }

    ControlNode* newNode(ControlNode::TNodeType type) {
        ControlNode* node = 0;

        switch (type) {
            case ControlNode::ntInstruction:
                node = new InstructionNode();
                break;

            case ControlNode::ntPhi:
                node = new PhiNode();
                break;

            case ControlNode::ntTau:
                node = new TauNode();
                break;
        }

        m_nodes.push_back(node);
        return node;
    }

    template<class T> T* newNode();

    ControlDomain* newDomain() {
        ControlDomain* domain = new ControlDomain;
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

private:
    ParsedMethod* m_parsedMethod;
    TDomainSet    m_domains;

    typedef std::list<ControlNode*> TNodeList;
    TNodeList     m_nodes;
};

template<> InstructionNode* ControlGraph::newNode<InstructionNode>();
template<> PhiNode* ControlGraph::newNode<PhiNode>();
template<> TauNode* ControlGraph::newNode<TauNode>();

} // namespace st

#endif
