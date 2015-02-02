
#include <visualization.h>

bool ControlGraphVisualizer::visitDomain(st::ControlDomain& domain) {
//     if (!firstDomain)
//         m_stream << "\t}                         \n" << std::endl; // closing subgraph

    firstDomain = false;

//     m_stream << "\n\tsubgraph cluster_" << domain.getBasicBlock()->getOffset() << " {\n";
    return st::NodeVisitor::visitDomain(domain);
}

std::string edgeStyle(st::ControlNode* from, st::ControlNode* to) {
    const st::InstructionNode* const fromInstruction = from->cast<st::InstructionNode>();
	const st::InstructionNode* const toInstruction   = to->cast<st::InstructionNode>();

    if ((fromInstruction && fromInstruction->getInstruction().isBranch()) ||
        (toInstruction && toInstruction->getArgumentsCount() == 0))
    {
        return "[color=\"grey\" style=\"dashed\"]";
    }

    return "";
}

bool ControlGraphVisualizer::visitNode(st::ControlNode& node) {
    const st::TNodeSet& inEdges  = node.getInEdges();
    const st::TNodeSet& outEdges = node.getOutEdges();

    // Processing incoming edges
    st::TNodeSet::iterator iEdge = inEdges.begin();
    for (; iEdge != inEdges.end(); ++iEdge) {
        if (isNodeProcessed(*iEdge))
            continue;

        m_stream << "\t\t" << (*iEdge)->getIndex() << " -> " << node.getIndex() << edgeStyle(*iEdge, &node) << ";\n";
    }

    // Processing outgoing edges
    iEdge = outEdges.begin();
    for (; iEdge != outEdges.end(); ++iEdge) {
        if (isNodeProcessed(*iEdge))
            continue;

        m_stream << "\t\t" << node.getIndex() << " -> " << (*iEdge)->getIndex() << edgeStyle(&node, *iEdge) << ";\n";
    }

    // Processing argument edges
    if (const st::InstructionNode* const instruction = node.cast<st::InstructionNode>()) {
        const std::size_t argsCount = instruction->getArgumentsCount();
        for (std::size_t index = 0; index < argsCount; index++) {
            m_stream << "\t\t" << node.getIndex() << " -> " << instruction->getArgument(index)->getIndex() << " [";

            if (argsCount > 1)
                m_stream << "label=" << index;

            m_stream << " labelfloat=true color=\"blue\" fontcolor=\"blue\" style=\"dashed\" constraint=false];\n";
        }
    }

    markNode(&node);
    return st::NodeVisitor::visitNode(node);
}

bool ControlGraphVisualizer::isNodeProcessed(st::ControlNode* node) {
    return m_processedNodes.find(node) != m_processedNodes.end();
}

void ControlGraphVisualizer::markNode(st::ControlNode* node) {
    // Setting node label
    std::string label;
    std::string color;

    switch (node->getNodeType()) {
        case st::ControlNode::ntPhi:
            label = "Phi ";
            color = "grey";
            break;

        case st::ControlNode::ntTau:
            label = "Tau ";
            color = "green";
            break;

        case st::ControlNode::ntInstruction: {
            st::InstructionNode* instruction = node->cast<st::InstructionNode>();
            label = instruction->getInstruction().toString();

            if (instruction->getInstruction().getOpcode() == opcode::sendMessage) {
                TSymbolArray* const literals = m_graph->getParsedMethod()->getOrigin()->literals;
                TSymbol* const name = literals->getField(instruction->getInstruction().getArgument());
                label += " " + name->toString();
            }

            const bool isTerminator = instruction->getInstruction().isTerminator();
            const bool isEntryPoint = (instruction == instruction->getDomain()->getEntryPoint());

            if (isTerminator && isEntryPoint)
                color = "red"; // color = "green3;red";
            else if (isEntryPoint)
                color = "green3";
            else if (isTerminator)
                color = "red";
        } break;

        default:
            ;
    }

    m_stream << "\t\t" << node->getIndex() << " [shape=box label=\"" << node->getDomain()->getBasicBlock()->getOffset() << "." << node->getIndex() << " : " << label << "\" color=\"" << color << "\"];\n";
//     m_stream << "\t\t " << node->getIndex() << "[label=\"" << node->getIndex() /*<< " : " << label */<< "\" color=\"" << color << "\"];\n";
    m_processedNodes[node] = true;
}

void ControlGraphVisualizer::finish() {
//     if (!firstDomain)
//         m_stream << "\t}                         \n";

    m_stream << "}                         \n";
    m_stream.close();
}
