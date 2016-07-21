#ifndef LLST_INFERENCE_H_INCLUDED
#define LLST_INFERENCE_H_INCLUDED

#include <vm.h>
#include <analysis.h>

#include <algorithm>

namespace type {


class Type {
public:
    enum TKind {
        tkUndefined = 0,
        tkLiteral,
        tkMonotype,
        tkComposite,
        tkArray,
        tkPolytype
        // TODO tkBlock
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
    std::string toString(bool subtypesOnly = true) const;

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

    bool isUndefined() const { return m_kind == tkUndefined; }
    bool isLiteral() const { return m_kind == tkLiteral; }
    bool isMonotype() const { return m_kind == tkMonotype; }
    bool isComposite() const { return m_kind == tkComposite; }
    bool isArray() const { return m_kind == tkArray; }
    bool isPolytype() const { return m_kind == tkPolytype; }

    bool isBlock() const {
        return
            isMonotype() &&
            (m_value == globals.blockClass) &&
            !m_subTypes.empty();
    }

    const TSubTypes& getSubTypes() const { return m_subTypes; }

    Type& pushSubType(const Type& type) { m_subTypes.push_back(type); return m_subTypes.back(); }

    void addSubType(const Type& type) {
        if (std::find(m_subTypes.begin(), m_subTypes.end(), type) == m_subTypes.end())
            m_subTypes.push_back(type);
    }

    Type flatten() const;

    Type fold() const {
        if (m_kind != tkComposite)
            return *this;

        const std::size_t subtypesCount = m_subTypes.size();
        if (! subtypesCount)
            return *this;

        Type result = m_subTypes.at(0);
        for (std::size_t i = 1; i < subtypesCount; i++) {
            if (m_subTypes.at(i).m_kind == tkComposite)
                result &= m_subTypes.at(i).fold();
            else
                result &= m_subTypes.at(i);
        }

        return result;
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
            else if (other.m_subTypes[index] < m_subTypes[index])
                return false;
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

    //     ?         |      _      ->     _
    //     *         |      _      ->     (*, _)

    //     1         |      1      ->     1
    //     1         |      2      ->     (1, 2)
    //     A         |      B      ->     (A, B)
    //    (A)        |     (B)     ->     (A, B)
    //    (A)        |     (B,C)   ->     (A, B, C)
    //    Block1     |     Block2  ->     (Block1, Block2)
    Type& operator |= (const Type& other) {
        if (*this == other)
            return *this;

        if (m_kind != tkComposite) {
            Type composite(tkComposite);
            composite.addSubType(*this);
            *this = composite;
        }

        if (other.m_value == globals.blockClass) {
            addSubType(other);
            return *this;
        }

        if (other.m_kind == tkComposite) {
            for (std::size_t index = 0; index < other.m_subTypes.size(); index++)
                addSubType(other[index]);

            return *this;
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
    typedef std::set<Type> TTypeSet;
    void flattenInto(TTypeSet& typeSet) const;

private:
    TKind     m_kind;
    TObject*  m_value;
    TSubTypes m_subTypes;
};

typedef std::size_t TNodeIndex;
typedef std::map<TNodeIndex, Type> TTypeMap;

inline std::string getQualifiedMethodName(TMethod* method, const Type& arguments) {
    return
        arguments.toString() + "::" +
        method->klass->name->toString() + ">>" +
        method->name->toString();
}

class InferContext {
public:
    InferContext(TMethod* method, std::size_t index, const Type& arguments) :
        m_method(method),
        m_index(index),
        m_arguments(arguments),
        m_returnType(Type::tkComposite),
        m_recursionKind(rkUnknown)
    {}

    std::string getQualifiedName() const { return getQualifiedMethodName(m_method, m_arguments); }

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

    Type& getRawReturnType() { return m_returnType; }

    const Type& getReturnType() const {
        const std::size_t subtypesCount = m_returnType.getSubTypes().size();
        return (subtypesCount == 1) ? m_returnType[0] : m_returnType;
    }

    Type getSingleReturnType() const { return m_returnType.fold(); }

    Type& getInstructionType(TNodeIndex index) { return m_types[index]; }
    Type& operator[] (TNodeIndex index) { return m_types[index]; }
    Type& operator[] (const st::ControlNode& node) { return getInstructionType(node.getIndex()); }

    // variable index -> aggregated type
    typedef std::size_t TVariableIndex;
    typedef std::map<TVariableIndex, Type> TVariableMap;

    // capture site index -> captured context types
    typedef std::size_t TSiteIndex;
    typedef std::map<TSiteIndex, TVariableMap> TBlockClosures;

    TBlockClosures& getBlockClosures() { return m_blockClosures; }
    void resetClosures() { m_blockClosures.clear(); }

    enum TRecursionKind {
        rkUnknown = 0,
        rkYes,
        rkNo
    };

    TRecursionKind getRecursionKind() const { return m_recursionKind; }
    void setRecursionKind(TRecursionKind value) { m_recursionKind = value; }


    class ContextCompare {
    public:
        bool operator() (const InferContext* a, const InferContext* b) const {
            return a->getIndex() < b->getIndex();
        }
    };

    typedef std::set<InferContext*, ContextCompare> TContextSet;
    TContextSet& getReferredContexts() { return m_referredContexts; }
    const TContextSet& getReferredContexts() const { return m_referredContexts; }

private:
    TMethod* const    m_method;
    const std::size_t m_index;
    const Type        m_arguments;
    TTypeMap          m_types;
    Type              m_returnType;

    TBlockClosures    m_blockClosures;
    TRecursionKind    m_recursionKind;
    TContextSet       m_referredContexts;
};

struct TContextStack {
    InferContext&  context;
    TContextStack* parent;

    TContextStack(InferContext& context, TContextStack* parent = 0)
        : context(context), parent(parent) {}
};

class TypeSystem {
public:
    TypeSystem(SmalltalkVM& vm) : m_vm(vm), m_lastContextIndex(1) {}

    typedef TSymbol* TSelector;

    InferContext* inferMessage(
        TSelector selector,
        const Type& arguments,
        TContextStack* parent,
        bool sendToSuper = false);

    InferContext* inferBlock(Type& block, const Type& arguments, TContextStack* parent);
    st::ControlGraph* getMethodGraph(TMethod* method);
    st::ControlGraph* getBlockGraph(st::ParsedBlock* parsedBlock);

    void dumpAllContexts() const;
    void drawCallGraph() const;

private:
    typedef std::pair<st::ParsedBytecode*, st::ControlGraph*> TGraphEntry;
    typedef std::map<TMethod*, TGraphEntry> TGraphCache;

    typedef std::map<Type, InferContext*>    TContextMap;
    typedef std::map<TSelector, TContextMap> TContextCache;

    // [Block, Args] -> block context
    typedef std::map<Type, InferContext*> TBlockCache;

    typedef std::map<st::ParsedBlock*, st::ControlGraph*> TBlockGraphCache;

private:
    SmalltalkVM&     m_vm; // TODO Image must be enough

    TGraphCache      m_graphCache;
    TContextCache    m_contextCache;
    TBlockCache      m_blockCache;
    TBlockGraphCache m_blockGraphCache;

    std::size_t      m_lastContextIndex;
};

class TypeAnalyzer {
public:
    TypeAnalyzer(TypeSystem& system, st::ControlGraph& graph, TContextStack& contextStack) :
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
    void dumpTypes(const InferContext& context);
    std::string getMethodName();
    bool basicRun();

    void processInstruction(st::InstructionNode& instruction);
    void processTau(const st::TauNode& tau);

    Type& processPhi(const st::PhiNode& phi);
    Type& getArgumentType(const st::InstructionNode& instruction, std::size_t index = 0);

    void walkComplete();

    void doPushConstant(const st::InstructionNode& instruction);
    void doPushLiteral(const st::InstructionNode& instruction);
    void doPushArgument(const st::InstructionNode& instruction);

    void doPushTemporary(const st::InstructionNode& instruction);
    void doAssignTemporary(const st::InstructionNode& instruction);

    void doPushBlock(const st::InstructionNode& instruction);

    void doSendUnary(const st::InstructionNode& instruction);
    void doSendBinary(st::InstructionNode& instruction);
    void doMarkArguments(const st::InstructionNode& instruction);
    void doSendMessage(st::InstructionNode& instruction, bool sendToSuper = false);

    void doPrimitive(const st::InstructionNode& instruction);
    void doSpecial(st::InstructionNode& instruction);

private:
    void captureContext(st::InstructionNode& instruction, Type& arguments);
    InferContext* getMethodContext();

    void fillLinkerClosures();

private:

    class Walker : public st::GraphWalker {
    public:
        Walker(TypeAnalyzer& analyzer) : analyzer(analyzer) {}

    private:
        TVisitResult visitNode(st::ControlNode& node, const TPathNode*) {
            if (st::InstructionNode* const instruction = node.cast<st::InstructionNode>())
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
    st::ControlGraph&  m_graph;
    TContextStack& m_contextStack;
    InferContext&  m_context;

    st::TauLinker  m_tauLinker;
    Walker         m_walker;

    typedef std::map<InferContext::TSiteIndex, st::InstructionNode*> TSiteMap;
    TSiteMap m_siteMap;

    bool m_baseRun;
    bool m_literalBranch;

    const Type* m_blockType;
};

inline type::Type createArgumentsType(TObjectArray* arguments) {
    type::Type result(globals.arrayClass, type::Type::tkArray);

    for (std::size_t i = 0; i < arguments->getSize(); i++) {
        TObject* const argument = arguments->getField(i);
        TClass* const klass = isSmallInteger(argument) ? globals.smallIntClass : argument->getClass();
        result.pushSubType(type::Type(klass));
    }

    return result;
}

} // namespace type

#endif // LLST_INFERENCE_H_INCLUDED
