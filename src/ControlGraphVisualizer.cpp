
#include <visualization.h>

bool ControlGraphVisualizer::visitDomain(st::ControlDomain& domain) {
    // TODO Create domain frame

    return st::NodeVisitor::visitDomain(domain);
}

bool ControlGraphVisualizer::visitNode(st::ControlNode& node) {
    ogdf::NodeElement* element = getNodeFor(&node);

    const st::TNodeSet& inEdges  = node.getInEdges();
    const st::TNodeSet& outEdges = node.getOutEdges();

    // Processing incoming edges
    st::TNodeSet::iterator iEdge = inEdges.begin();
    for (; iEdge != inEdges.end(); ++iEdge)
        addEdge(*iEdge, &node);

    // Processing outgoing edges
    iEdge = outEdges.begin();
    for (; iEdge != outEdges.end(); ++iEdge)
        addEdge(&node, *iEdge);

    // Processing argument edges
    if (const st::InstructionNode* instruction = node.cast<st::InstructionNode>()) {
        for (std::size_t index = 0; index < instruction->getArgumentsCount(); index++) {
            ogdf::NodeElement* argument = getNodeFor(instruction->getArgument(index));
            ogdf::EdgeElement* edge     = m_ogdfGraph->newEdge(element, argument);

            m_ogdfAttributes->labelEdge(edge) = index;
            m_ogdfAttributes->colorEdge(edge) = "blue";
            m_ogdfAttributes->arrowEdge(edge) = ogdf::GraphAttributes::first;
        }
    }

    return st::NodeVisitor::visitNode(node);
}

ogdf::NodeElement* ControlGraphVisualizer::getNodeFor(st::ControlNode* node) {
    TNodeMap::iterator iNode = m_nodes.find(node);

    if (iNode != m_nodes.end())
        return iNode->second;

    ogdf::NodeElement* element = m_ogdfGraph->newNode(node->getIndex());
    m_ogdfAttributes->shapeNode(element) = ogdf::GraphAttributes::oval;
    m_ogdfAttributes->width(element)  = 32;
    m_ogdfAttributes->height(element) = 32;

    ogdf::String& label = m_ogdfAttributes->labelNode(element);

    switch (node->getNodeType()) {
         case st::ControlNode::ntPhi:
             label = "Phi";
             m_ogdfAttributes->colorNode(element) = "grey";
             break;

         case st::ControlNode::ntTau:
             label = "Tau";
             m_ogdfAttributes->colorNode(element) = "green";
             break;

         case st::ControlNode::ntInstruction:
             //label = node.cast<st::InstructionNode>()->getInstruction().toString();
             m_ogdfAttributes->colorNode(element) = "blue";

         default:
             label = node->getIndex();
    }

    m_nodes[node] = element;
    return element;
}

void ControlGraphVisualizer::addEdge(st::ControlNode* from, st::ControlNode* to) {
    ogdf::NodeElement* fromElement = getNodeFor(from);
    ogdf::NodeElement* toElement   = getNodeFor(to);

    ogdf::EdgeElement* edge = m_ogdfGraph->searchEdge(fromElement, toElement);
    if (edge) // Skipping if edge already exists
        return;

    edge = m_ogdfGraph->newEdge(fromElement, toElement);
    m_ogdfAttributes->arrowEdge(edge) = ogdf::GraphAttributes::first;

    const st::InstructionNode* fromInstruction = from->cast<st::InstructionNode>();
    const st::InstructionNode* toInstruction   = to->cast<st::InstructionNode>();

    if ((fromInstruction && fromInstruction->getInstruction().isBranch()) ||
        (toInstruction && toInstruction->getArgumentsCount() == 0))
    {
        m_ogdfAttributes->colorEdge(edge) = "grey";
        m_ogdfAttributes->styleEdge(edge) = ogdf::GraphAttributes::esDash;
    }

    if (to->getNodeType() == st::ControlNode::ntPhi)
        m_ogdfAttributes->colorEdge(edge) = "grey";
}

void ControlGraphVisualizer::writeGraphTo(const std::string& fileName) {
    m_ogdfGraph->writeGML(fileName.c_str());
}
