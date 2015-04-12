
#include <visualization.h>

bool ControlGraphVisualizer::visitDomain(st::ControlDomain& /*domain*/) {
//     if (!firstDomain)
//         m_stream << "\t}                         \n" << std::endl; // closing subgraph

    firstDomain = false;

//     m_stream << "\n\tsubgraph cluster_" << domain.getBasicBlock()->getOffset() << " {\n";
//     return st::NodeVisitor::visitDomain(domain);
    return false;
}

std::string edgeStyle(st::ControlNode* from, st::ControlNode* to) {
    const st::InstructionNode* const fromInstruction = from->cast<st::InstructionNode>();
	const st::InstructionNode* const toInstruction   = to->cast<st::InstructionNode>();

    if (from->getNodeType() == st::ControlNode::ntPhi && to->getNodeType() == st::ControlNode::ntPhi)
        return "[style=invis color=red constraint=false]";

    if (fromInstruction && fromInstruction->getInstruction().isBranch())
        return "[color=\"grey\" style=\"dashed\"]";

    if (toInstruction && toInstruction->getArgumentsCount() == 0)
        return "[color=\"black\" style=\"dashed\" ]";

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
    } else if (const st::PhiNode* const phi = node.cast<st::PhiNode>()) {

        m_stream << "\t\t" << phi->getIndex() << " -> " << phi->getDomain()->getEntryPoint()->getIndex()  << " ["
            << "labelfloat=true color=\"blue\" fontcolor=\"blue\" style=\"invis\" constraint=true ];\n";

        const st::PhiNode::TIncomingList& incomingList = phi->getIncomingList();
        for (std::size_t index = 0; index < incomingList.size(); index++) {
            const st::PhiNode::TIncoming& incoming = incomingList[index];

            m_stream << "\t\t" << incoming.node->getIndex() << " -> " << node.getIndex() << " ["
                << "dir=back labelfloat=true color=\"blue\" fontcolor=\"blue\" style=\"dashed\" constraint=true ];\n";

            m_stream << "\t\t" << incoming.domain->getTerminator()->getIndex() << " -> " << phi->getIndex() << " ["
                << "style=\"invis\" constraint=true ];\n";

//             m_stream << "\t\t" << node.getIndex() << " -> " << incoming.value->getIndex() << " ["
//                      << " labelfloat=true color=\"blue\" fontcolor=\"blue\" style=\"dashed\" constraint=false ];\n";
        }
    }

    markNode(&node);
    return st::PlainNodeVisitor::visitNode(node);
}

bool ControlGraphVisualizer::isNodeProcessed(st::ControlNode* node) {
    return m_processedNodes.find(node) != m_processedNodes.end();
}

void ControlGraphVisualizer::markNode(st::ControlNode* node) {
    // Setting node label
    std::string label;
    std::string color;
    std::string shape = "box";

    switch (node->getNodeType()) {
        case st::ControlNode::ntPhi:
            //label = "Phi ";
            color = "grey";
            shape = "oval";
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
            } else if (instruction->getInstruction().isBranch()) {
                std::stringstream ss;
                ss << " " << instruction->getInstruction().getExtra();
                label += ss.str();
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

    if (node->getNodeType() == st::ControlNode::ntPhi)
        m_stream << "\t\t" << node->getIndex() << " [label=\"" << node->getIndex() << "\" color=\"" << color << "\"];\n";
    else
        m_stream << "\t\t" << node->getIndex() << " [shape=\"" << shape << "\" label=\"" << (node->getDomain() ? node->getDomain()->getBasicBlock()->getOffset() : 666) << "." << node->getIndex() << " : " << label << "\" color=\"" << color << "\"];\n";

    m_processedNodes[node] = true;
}

void ControlGraphVisualizer::finish() {
//     if (!firstDomain)
//         m_stream << "\t}                         \n";

    m_stream << "}                         \n";
    m_stream.close();
}
