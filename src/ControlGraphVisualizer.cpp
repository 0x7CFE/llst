
#include <visualization.h>

#include <ogdf/layered/SugiyamaLayout.h>
#include <ogdf/layered/OptimalRanking.h>
#include <ogdf/layered/MedianHeuristic.h>
#include <ogdf/layered/OptimalHierarchyLayout.h>

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

void ControlGraphVisualizer::setNodeProperties(st::ControlNode* node, ogdf::NodeElement* element) {
    // Setting basic attributes
    m_ogdfAttributes->shapeNode(element) = ogdf::GraphAttributes::oval;
    m_ogdfAttributes->width(element)  = 32;
    m_ogdfAttributes->height(element) = 32;

    // Calculating node position
    uint32_t newX = 0;
    uint32_t newY = 0;

    // We do not want to apply complex layout because our graph is pretty straighforward.
    // So placing nodes in natural order but stacking argument nodes where possible.
    if (node->getInEdges().size()) {
        // Shifting to the next colon
        newX = m_lastX + 64;
        newY = 0;
    } else {
        // Stacking it with previous one.
        newX = m_lastX;
        newY = m_lastY + 64;
    }

    m_ogdfAttributes->x(element) = newX;
    m_ogdfAttributes->y(element) = newY;

    m_lastX = newX;
    m_lastY = newY;

    // Setting node label
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
}

ogdf::NodeElement* ControlGraphVisualizer::getNodeFor(st::ControlNode* node) {
    TNodeMap::iterator iNode = m_nodes.find(node);

    if (iNode != m_nodes.end())
        return iNode->second;

    ogdf::NodeElement* element = m_ogdfGraph->newNode(node->getIndex());
    setNodeProperties(node, element);

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

void ControlGraphVisualizer::applyLayout() {
    ogdf::SugiyamaLayout SL;
    SL.setRanking(new ogdf::OptimalRanking);
    SL.setCrossMin(new ogdf::MedianHeuristic);

//     ogdf::OptimalHierarchyLayout* ohl = new ogdf::OptimalHierarchyLayout;
//     ohl->layerDistance(30.0);
//     ohl->nodeDistance(25.0);
//     ohl->weightBalancing(0.8);
//     SL.setLayout(ohl);

    SL.call(*m_ogdfAttributes);
}

void ControlGraphVisualizer::writeGraphTo(const std::string& fileName) {
    m_ogdfGraph->writeGML(fileName.c_str());
}
