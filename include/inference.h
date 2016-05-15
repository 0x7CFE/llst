#ifndef LLST_INFERENCE_H_INCLUDED
#define LLST_INFERENCE_H_INCLUDED

#include <vm.h>
#include <analysis.h>

namespace type {

using namespace st;

class Type {
public:
    enum TKind {
        tkUndefined = 0,
        tkLiteral,
        tkMonotype,
        tkComposite,
        tkArray,
        tkPolytype
    };

    // Return a string representation of a type:
    // Kind             Representation      Example
    // tkUndefined      ?                   ?
    // tkPolytype       *                   *
    // tkLiteral        literal value       42
    // tkMonotype       (class name)        (SmallInt)
    // tkComposite      (class name, ...)   (SmallInt, *)
    // tkArray          class name [...]    Array[String, *, (*, *), (True, False)]
    std::string toString() const;

    Type(TKind kind = tkUndefined) : m_kind(kind), m_value(0) {}
    Type(TObject* literal) { set(literal); }
    Type(TClass* klass) { set(klass); }

    void setKind(TKind kind) { m_kind = kind; }
    TKind getKind() const { return m_kind; }
    TObject* getValue() const { return m_value; }

    void reset() {
        m_kind  = tkUndefined;
        m_value = 0;
        m_subTypes.clear();
    }

    void set(TObject* literal, TKind kind = tkLiteral) {
        m_kind  = kind;
        m_value = literal;
    }

    void set(TClass* klass, TKind kind = tkMonotype) {
        m_kind  = kind;
        m_value = klass;
    }

    typedef std::vector<Type> TSubTypes;
    const TSubTypes& getSubTypes() const { return m_subTypes; }
    const Type& operator[] (std::size_t index) const { return m_subTypes[index]; }

    void addSubType(const Type& type) { m_subTypes.push_back(type); }

private:
    TKind     m_kind;
    TObject*  m_value;
    TSubTypes m_subTypes;
};

typedef std::vector<Type> TTypeList;

class CallContext {
public:
    CallContext(std::size_t index, const Type& arguments, std::size_t nodeCount)
        : m_index(index), m_arguments(arguments)
    {
        m_instructions.resize(nodeCount);
    }

    std::size_t getIndex() const { return m_index; }

    const Type& getArgument(std::size_t index) const {
        static const Type polytype(Type::tkPolytype);

        if (m_arguments.getKind() != Type::tkPolytype)
            return m_arguments[index];
        else
            return polytype;
    }
    const Type& getArguments() const { return m_arguments; }

    Type& getReturnType() { return m_returnType; }

    Type& getInstructionType(std::size_t index) { return m_instructions[index]; }
    Type& operator[] (const ControlNode& node) { return getInstructionType(node.getIndex()); }


private:
    const std::size_t m_index;
    const Type m_arguments;
    TTypeList  m_instructions;
    Type       m_returnType;
};

class TypeSystem {
public:
    CallContext* newCallContext(TMethod* method, const Type& arguments = Type(Type::tkPolytype));

    CallContext* getCallContext(std::size_t index);
};

class TypeAnalyzer {
public:
    TypeAnalyzer(ControlGraph& graph, CallContext& context)
        : m_graph(graph), m_context(context) {}

    void run() {
        if (m_graph.isEmpty())
            return;

        Walker walker(*this);
        walker.run(*m_graph.nodes_begin(), Walker::wdForward);
    }

private:
    ControlGraph& m_graph;
    CallContext&  m_context;

private:
    void processInstruction(const InstructionNode& instruction);
    void processPhi(const PhiNode& phi);
    void processTau(const TauNode& tau);
    void walkComplete();

    void doMarkArguments(const InstructionNode& instruction);
    void doPushConstant(const InstructionNode& instruction);
    void doPushLiteral(const InstructionNode& instruction);
    void doSendUnary(const InstructionNode& instruction);
    void doSendBinary(const InstructionNode& instruction);

private:

    class Walker : public GraphWalker {
    public:
        Walker(TypeAnalyzer& analyzer) : analyzer(analyzer) {}

    private:
        TVisitResult visitNode(ControlNode& node, const TPathNode*) {
            switch (node.getNodeType()) {
                case ControlNode::ntInstruction:
                    analyzer.processInstruction(static_cast<const InstructionNode&>(node));
                    break;

                case ControlNode::ntPhi:
                    analyzer.processPhi(static_cast<const PhiNode&>(node));
                    break;

                case ControlNode::ntTau:
                    analyzer.processTau(static_cast<const TauNode&>(node));
                    break;
            }

            return vrKeepWalking;
        }

        void nodesVisited() {
            analyzer.walkComplete();
        }

    private:
        TypeAnalyzer& analyzer;
    };

};

} // namespace type

#endif
