#ifndef LLST_INFERENCE_H_INCLUDED
#define LLST_INFERENCE_H_INCLUDED

#include <vm.h>
#include <analysis.h>

#include <algorithm>

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

    enum TBlockSubtypes {
        bstOrigin = 0,
        bstOffset,
        bstArgIndex,
        bstContextIndex,
        bstReadsTemps,
        bstWritesTemps,
        bstCaptureIndex
    };

    // Return a string representation of a type:
    // Kind             Representation      Example
    // tkUndefined      ?                   ?
    // tkPolytype       *                   *
    // tkLiteral        literal value       42
    // tkMonotype       (class name)        (SmallInt)
    // tkComposite      (class name, ...)   (SmallInt, *)
    // tkArray          class name [...]    Array[String, *, (*, *), (True, False)]
    std::string toString(bool subtypesOnly = false) const;

    Type(TKind kind = tkUndefined) : m_kind(kind), m_value(0) {}
    Type(TObject* literal, TKind kind = tkLiteral) { set(literal, kind); }
    Type(TClass* klass, TKind kind = tkMonotype) { set(klass, kind); }

    Type(const Type& copy) : m_kind(copy.m_kind), m_value(copy.m_value), m_subTypes(copy.m_subTypes) {}

    void setKind(TKind kind) { m_kind = kind; }
    TKind getKind() const { return m_kind; }
    TObject* getValue() const { return m_value; }

    typedef std::vector<Type> TSubTypes;

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

    const TSubTypes& getSubTypes() const { return m_subTypes; }

    Type& pushSubType(const Type& type) { m_subTypes.push_back(type); return m_subTypes.back(); }

    void addSubType(const Type& type) {
        if (std::find(m_subTypes.begin(), m_subTypes.end(), type) == m_subTypes.end())
            m_subTypes.push_back(type);
    }

    const Type& operator [] (std::size_t index) const { return m_subTypes[index]; }
    Type& operator [] (std::size_t index) { return m_subTypes[index]; }

    bool operator < (const Type& other) const {
        if (m_kind != other.m_kind)
            return m_kind < other.m_kind;

        if (m_value != other.m_value)
            return m_value < other.m_value;

        if (m_subTypes.size() != other.m_subTypes.size())
            return m_subTypes.size() < other.m_subTypes.size();

        for (std::size_t index = 0; index < m_subTypes.size(); index++) {
            if (m_subTypes[index] < other.m_subTypes[index])
                return true;
        }

        return false;
    }

    bool operator == (const Type& other) const {
        if (m_kind != other.m_kind)
            return false;

        if (m_value != other.m_value)
            return false;

        if (m_subTypes != other.m_subTypes)
            return false;

        return true;
    }

    Type& operator = (const Type& other) {
        m_kind     = other.m_kind;
        m_value    = other.m_value;
        m_subTypes = other.m_subTypes;

        return *this;
    }

    Type operator | (const Type& other) const { return Type(*this) |= other; }
    Type operator & (const Type& other) const { return Type(*this) &= other; }

    Type& operator |= (const Type& other) {
        if (*this == other)
            return *this;

        // FIXME May lose information
        if (m_kind == tkUndefined)
            return *this = other;

        if (m_kind != tkComposite) {
            m_subTypes.push_back(*this);
            m_kind = tkComposite;
        }

        if (other.m_kind != tkComposite) {
            addSubType(other);
        } else {
            for (std::size_t index = 0; index < other.m_subTypes.size(); index++)
                addSubType(other[index]);
        }

        return *this;
    }

    //     ?       &      _      ->     ?
    //     *       &      _      ->     *

    //     2       &      2      ->     2
    //     2       &      3      -> (SmallInt)
    //     2       &  (SmallInt) -> (SmallInt)
    //    true     &     false   -> (Boolean)

    //  (2, 3)     &  (SmallInt) -> (SmallInt)
    // (SmallInt)  &  (SmallInt) -> (SmallInt)
    // (SmallInt)  &  (SmallInt) -> (SmallInt)
    // (SmallInt)  &     true    ->     *
    // (SmallInt)  &   (Object)  ->     *

    // Array[2,3]  &   (Array)   ->  (Array)
    //
    Type& operator &= (const Type& other) {
        if (other.m_kind == tkUndefined || other.m_kind == tkPolytype)
            return *this = Type((m_kind == tkUndefined) ? tkUndefined : other.m_kind);

        switch (m_kind) {
            case tkUndefined:
            case tkPolytype:
                return *this = Type((other.m_kind == tkUndefined) ? tkUndefined : m_kind);

            case tkLiteral:
                if (m_value == other.m_value) { // 2 & 3
                    return *this; // 2 & 2
                } else {
                    // TODO true & false -> (Boolean)
                    TClass* const klass = isSmallInteger(m_value) ? globals.smallIntClass : m_value->getClass();
                    return *this = (Type(klass) &= other);
                }

            case tkMonotype: {
                if (m_value == other.m_value) // (SmallInt) & (Object)
                    return *this; // (SmallInt) & (SmallInt)

                TObject* const otherValue = other.m_value;
                TClass*  const otherKlass = isSmallInteger(otherValue) ? globals.smallIntClass : otherValue->getClass();
                if (other.m_kind == tkLiteral && m_value == otherKlass)
                    return *this; // (SmallInt) & 42
                else
                    return *this = Type(tkPolytype);
            }

            case tkArray:
                if (other.m_kind == tkArray) { // Array[2, 3] & Array[2, 3]
                    if (m_value == other.m_value) { // Array[true, false] & Object[true, false]
                        if (*this == other)
                            return *this;
                        else
                            return *this = Type(m_value, tkMonotype); // Array[2, 3] & (Array)
                    }
                }
                return *this = (Type(m_value) &= other);

            case tkComposite:
                if (m_subTypes.empty()) {
                    reset();
                    return *this;
                }

                Type& result = m_subTypes[0];
                for (std::size_t index = 1; index < m_subTypes.size(); index++) {
                    result &= m_subTypes[index];

                    if (result.getKind() == tkUndefined || result.getKind() == tkPolytype)
                        return *this = result;
                }

                return *this = result;
        }

        reset();
        return *this;
    }

private:
    TKind     m_kind;
    TObject*  m_value;
    TSubTypes m_subTypes;
};

typedef std::size_t TNodeIndex;
typedef std::map<TNodeIndex, Type> TTypeMap;

class InferContext {
public:
    InferContext(TMethod* method, std::size_t index, const Type& arguments)
        : m_method(method), m_index(index), m_arguments(arguments) {}

    TMethod* getMethod() const { return m_method; }
    std::size_t getIndex() const { return m_index; }

    const Type& getArgument(std::size_t index) const {
        static const Type polytype(Type::tkPolytype);

        if (m_arguments.getKind() != Type::tkPolytype)
            return m_arguments[index];
        else
            return polytype;
    }

    const Type& getArguments() const { return m_arguments; }
    const TTypeMap& getTypes() const { return m_types; }
    void resetTypes() { m_types.clear(); }

    Type& getReturnType() { return m_returnType; }

    Type& getInstructionType(TNodeIndex index) { return m_types[index]; }
    Type& operator[] (TNodeIndex index) { return m_types[index]; }
    Type& operator[] (const ControlNode& node) { return getInstructionType(node.getIndex()); }

    // variable index -> aggregated type
    typedef std::size_t TVariableIndex;
    typedef std::map<TVariableIndex, Type> TVariableMap;

    // capture site index -> captured context types
    typedef std::size_t TSiteIndex;
    typedef std::map<TSiteIndex, TVariableMap> TBlockClosures;

    TBlockClosures& getBlockClosures() { return m_blockClosures; }
    void resetClosures() { m_blockClosures.clear(); }

private:
    TMethod* const    m_method;
    const std::size_t m_index;
    const Type        m_arguments;
    TTypeMap          m_types;
    Type              m_returnType;

    TBlockClosures    m_blockClosures;
};

struct TContextStack {
    InferContext&  context;
    TContextStack* parent;

    TContextStack(InferContext& context, TContextStack* parent = 0)
        : context(context), parent(parent) {}
};

class TypeSystem {
public:
    TypeSystem(SmalltalkVM& vm) : m_vm(vm), m_lastContextIndex(0) {}

    typedef TSymbol* TSelector;

    InferContext* inferMessage(
        TSelector selector,
        const Type& arguments,
        TContextStack* parent,
        bool sendToSuper = false);

    InferContext* inferBlock(Type& block, const Type& arguments, TContextStack* parent);

    ControlGraph* getControlGraph(TMethod* method);

private:
    typedef std::pair<ParsedBytecode*, ControlGraph*> TGraphEntry;
    typedef std::map<TMethod*, TGraphEntry> TGraphCache;

    typedef std::map<Type, InferContext*>    TContextMap;
    typedef std::map<TSelector, TContextMap> TContextCache;

private:
    SmalltalkVM&  m_vm; // TODO Image must be enough

    TGraphCache   m_graphCache;
    TContextCache m_contextCache;
    std::size_t   m_lastContextIndex;
};

class TypeAnalyzer {
public:
    TypeAnalyzer(TypeSystem& system, ControlGraph& graph, TContextStack& contextStack) :
        m_system(system),
        m_graph(graph),
        m_contextStack(contextStack),
        m_context(contextStack.context),
        m_tauLinker(m_graph),
        m_walker(*this)
    {
    }

    void run(const Type* blockType = 0);

private:
    std::string getMethodName();
    bool basicRun();

    void processInstruction(InstructionNode& instruction);
    void processTau(const TauNode& tau);

    Type& processPhi(const PhiNode& phi);
    Type& getArgumentType(const InstructionNode& instruction, std::size_t index = 0);

    void walkComplete();

    void doPushConstant(const InstructionNode& instruction);
    void doPushLiteral(const InstructionNode& instruction);
    void doPushArgument(const InstructionNode& instruction);

    void doPushTemporary(const InstructionNode& instruction);
    void doAssignTemporary(const InstructionNode& instruction);

    void doPushBlock(const InstructionNode& instruction);

    void doSendUnary(const InstructionNode& instruction);
    void doSendBinary(InstructionNode& instruction);
    void doMarkArguments(const InstructionNode& instruction);
    void doSendMessage(InstructionNode& instruction, bool sendToSuper = false);

    void doPrimitive(const InstructionNode& instruction);
    void doSpecial(InstructionNode& instruction);

private:
    void captureContext(InstructionNode& instruction, Type& arguments);
    InferContext* getMethodContext();

    void fillLinkerClosures();

private:

    class Walker : public GraphWalker {
    public:
        Walker(TypeAnalyzer& analyzer) : analyzer(analyzer) {}

    private:
        TVisitResult visitNode(ControlNode& node, const TPathNode*) {
            if (InstructionNode* const instruction = node.cast<InstructionNode>())
                analyzer.processInstruction(*instruction);

            return vrKeepWalking;
        }

        void nodesVisited() {
            analyzer.walkComplete();
        }

    private:
        TypeAnalyzer& analyzer;
    };

private:
    TypeSystem&    m_system;
    ControlGraph&  m_graph;
    TContextStack& m_contextStack;
    InferContext&  m_context;

    TauLinker      m_tauLinker;
    Walker         m_walker;

    typedef std::map<InferContext::TSiteIndex, InstructionNode*> TSiteMap;
    TSiteMap m_siteMap;

    bool m_baseRun;
    bool m_literalBranch;

    const Type* m_blockType;
};

} // namespace type

#endif
