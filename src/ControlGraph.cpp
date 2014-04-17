#include <analysis.h>

using namespace st;

template<> InstructionNode* ControlGraph::newNode<InstructionNode>() { return static_cast<InstructionNode*>(newNode(ControlNode::ntInstruction)); }
template<> PhiNode* ControlGraph::newNode<PhiNode>() { return static_cast<PhiNode*>(newNode(ControlNode::ntPhi)); }
template<> TauNode* ControlGraph::newNode<TauNode>() { return static_cast<TauNode*>(newNode(ControlNode::ntTau)); }

class GraphConstructor : public InstructionVisitor {
public:
    GraphConstructor(ControlGraph* graph) : InstructionVisitor(graph->getParsedMethod()), m_graph(graph) { }

    virtual bool visitBlock(BasicBlock& basicBlock) {
        m_currentDomain = m_graph->getDomainFor(&basicBlock);
        return InstructionVisitor::visitBlock(basicBlock);
    }

    virtual bool visitInstruction(const TSmalltalkInstruction& instruction) {
        // Initializing instruction node
        InstructionNode* newNode = m_graph->newNode<InstructionNode>();
        newNode->setInstruction(instruction);
        newNode->setDomain(m_currentDomain);
        m_currentDomain->addNode(newNode);

        // Processing instruction by adding references
        processNode(newNode);

        return true;
    }

private:
    void processNode(InstructionNode* node);
    void processSpecials(InstructionNode* node);
    void processPrimitives(InstructionNode* node);

    ControlGraph*  m_graph;
    ControlDomain* m_currentDomain;
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
        case opcode::sendUnary:
        case opcode::sendMessage:
            m_currentDomain->requestArgument(0, node);
            break;

        case opcode::sendBinary:
            m_currentDomain->requestArgument(1, node);
            m_currentDomain->requestArgument(0, node);
            break;

        case opcode::markArguments:
            for (uint32_t index = instruction.getArgument(); index > 0; )
                m_currentDomain->requestArgument(--index, node);
            break;

        case opcode::doSpecial:
            processSpecials(node);
            break;

        case opcode::doPrimitive:
            processPrimitives(node);
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

void ControlGraph::buildGraph()
{
    // Iterating through basic blocks of parsed method and constructing node domains
    GraphConstructor constructor(this);
    constructor.run();

    // TODO Linking nodes that requested argument during previous stage.
    //      Typically they're linked using phi nodes or a direct link if possible.
}
