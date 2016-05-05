
#include <visualization.h>
#include <unistd.h>
#include <errno.h>
#include <cstdlib>
#include <iomanip>

void * gnu_xmalloc(size_t size)
{
    void* value = malloc(size);
    if (value == 0) {
        perror("virtual memory exhausted");
        abort();
    }
    return value;
}

std::string gnu_getcwd() {
    size_t size = 100;

    while (true) {
        char *buffer = reinterpret_cast<char *>( gnu_xmalloc(size) );
        if ( getcwd(buffer, size) == buffer ) {
            std::string result(buffer);
            free(buffer);
            return result;
        }
        free(buffer);
        if (errno != ERANGE)
            return "";
        size *= 2;
    }
}

std::string escape_path(const std::string& path) {
    if (path.empty())
        return "<empty name>";
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (std::size_t i = 0; i < path.size(); ++i) {
        std::string::value_type c = path[i];

        if (iscntrl(c) || c == '/') {
            escaped << '%' << std::setw(2) << (int) c;
        } else {
            escaped << c;
        }
    }

    return escaped.str();
}

ControlGraphVisualizer::ControlGraphVisualizer(st::ControlGraph* graph, const std::string& fileName, const std::string& directory /*= "."*/)
    : st::PlainNodeVisitor(graph)
{
    std::string fullpath = directory + "/" + escape_path(fileName) + ".dot";
    m_stream.open(fullpath.c_str(), std::ios::out | std::ios::trunc);
    if (m_stream.fail()) {
        std::stringstream ss;
        ss << "Cannot open/truncate '" << fullpath << "' in '" << gnu_getcwd() << "'";
        throw std::ios_base::failure( ss.str() );
    }
    m_stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    m_stream << "digraph G2 {\n";
    firstDomain = true;
}

bool ControlGraphVisualizer::visitDomain(st::ControlDomain& /*domain*/) {
    firstDomain = false;
    return false;
}

std::string edgeStyle(st::ControlNode* from, st::ControlNode* to) {
//     const st::InstructionNode* const fromInstruction = from->cast<st::InstructionNode>();
    const st::InstructionNode* const toInstruction   = to->cast<st::InstructionNode>();

    if (from->getNodeType() == st::ControlNode::ntPhi && to->getNodeType() == st::ControlNode::ntPhi)
        return "[style=invis color=red constraint=false]";

//     if (from->getNodeType() == st::ControlNode::ntTau && to->getNodeType() == st::ControlNode::ntTau)
//         return "[style=invis color=red constraint=false]";

//     if (fromInstruction && fromInstruction->getInstruction().isBranch())
//         return "[color=\"grey\" style=\"dashed\"]";

    if (toInstruction && toInstruction->getArgumentsCount() == 0)
        return "[weight=100 color=\"black\" style=\"dashed\" ]";

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

        if (const st::InstructionNode* const instruction = (*iEdge)->cast<st::InstructionNode>()) {
            // Branch edges are handled separately
            if (instruction->getInstruction().isBranch())
                continue;
        }

        m_stream << "\t\t" << (*iEdge)->getIndex() << " -> " << node.getIndex() << edgeStyle(*iEdge, &node) << ";\n";
    }

    bool outEdgesProcessed = false;

    // Processing argument edges
    if (const st::InstructionNode* const instruction = node.cast<st::InstructionNode>()) {
        const std::size_t argsCount = instruction->getArgumentsCount();
        for (std::size_t index = 0; index < argsCount; index++) {
            m_stream << "\t\t" << instruction->getArgument(index)->getIndex() << " -> " << node.getIndex() << " [";

            if (argsCount > 1)
                m_stream << "label=" << index;

            m_stream << "dir=back weight=8 labelfloat=true color=\"blue\" fontcolor=\"blue\" style=\"dashed\" constraint=true];\n";


            /*if (const st::InstructionNode* const instructionArg = instruction->getArgument(index)->cast<st::InstructionNode>()) {
                if (const st::TauNode* const argumentTau = instructionArg->getTauNode()) {
                    m_stream << "\t\t" << argumentTau->getIndex() << " -> " << instructionArg->getIndex() << " ["
                        << "labelfloat=true color=\"green\" fontcolor=\"green\" style=\"dashed\" "
                        << "constraint=false ];\n";
                }
            }*/

        }

        if (const st::BranchNode* const branch = node.cast<st::BranchNode>()) {
            m_stream << "\t\t" << node.getIndex() << " -> " << branch->getTargetNode()->getIndex() << " [";
            m_stream << "weight=20 label=target labelfloat=true color=\"grey\" style=\"dashed\"];\n";

            if (branch->getSkipNode()) {
                m_stream << "\t\t" << node.getIndex() << " -> " << branch->getSkipNode()->getIndex() << " [";
                m_stream << "weight=20 label=skip labelfloat=true color=\"grey\" style=\"dashed\"];\n";
            }

            outEdgesProcessed = true;
        }

        /*if (const st::TauNode* const tau = instruction->getTauNode()) {
            const char* constraint = tau->getIncomingSet().size() == 1 ? "true" : "false";

            m_stream << "\t\t" << instruction->getIndex() << " -> " << tau->getIndex() << " ["
                << "labelfloat=true color=\"green\" fontcolor=\"green\" style=\"dashed\" "
                << "constraint=" << constraint << " ];\n";
        }*/

        /*st::TNodeSet::const_iterator iNode = instruction->getConsumers().begin();
        for (; iNode != instruction->getConsumers().end(); ++iNode) {
            if ((*iNode)->getNodeType() != st::ControlNode::ntTau)
                continue;

            m_stream << "\t\t" << (*iNode)->getIndex() << " -> " << tau->getIndex() << " ["
                << "labelfloat=true color=\"green\" fontcolor=\"green\" style=\"dashed\" constraint=false ];\n";
        }*/

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
        }
    } else if (const st::TauNode* const tau = node.cast<st::TauNode>()) {

        st::TNodeSet::const_iterator iNode = tau->getIncomingSet().begin();
        for (; iNode != tau->getIncomingSet().end(); ++iNode) {
//             if ((*iNode)->getNodeType() == st::ControlNode::ntInstruction)
//                 continue;

            if (tau->getKind() == st::TauNode::tkProvider) {
                m_stream << "\t\t" << (*iNode)->getIndex() << " -> " << tau->getIndex() << " ["
                    << "weight=15 dir=back labelfloat=true color=\"red\" fontcolor=\"red\" style=\"dashed\" constraint=true ];\n";
            } else {
                m_stream << "\t\t" << (*iNode)->getIndex() << " -> " << tau->getIndex() << " ["
                    << "weight=5 dir=back labelfloat=true color=\"grey\" fontcolor=\"green\" style=\"dashed\" constraint=true ];\n";
            }
        }

        iNode = tau->getConsumers().begin();
        for (; iNode != tau->getConsumers().end(); ++iNode) {
            if ((*iNode)->getNodeType() == st::ControlNode::ntTau)
                continue;

            m_stream << "\t\t" << tau->getIndex() << " -> " << (*iNode)->getIndex() << " ["
                << "weight=15 dir=back labelfloat=true color=\"green\" fontcolor=\"green\" style=\"dashed\" constraint=true ];\n";
        }
    }

    // Processing outgoing edges in generic way
    if (!outEdgesProcessed) {
        iEdge = outEdges.begin();
        for (; iEdge != outEdges.end(); ++iEdge) {
            if (isNodeProcessed(*iEdge))
                continue;

            m_stream << "\t\t" << node.getIndex() << " -> " << (*iEdge)->getIndex() << edgeStyle(&node, *iEdge) << ";\n";
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
            color = "blue";
            shape = "oval";
            break;

        case st::ControlNode::ntTau:
            label = "Tau ";
            color = (node->cast<st::TauNode>()->getKind() == st::TauNode::tkProvider) ? "red" : "green";
            shape = "oval";
            break;

        case st::ControlNode::ntInstruction: {
            st::InstructionNode* instruction = node->cast<st::InstructionNode>();
            label = instruction->getInstruction().toString();

            if (instruction->getInstruction().getOpcode() == opcode::sendMessage) {
                TSymbolArray* const literals = m_graph->getParsedMethod()->getOrigin()->literals;
                uint32_t litIdx = instruction->getInstruction().getArgument();
                std::string name;
                if (literals)
                    name = literals->getField(litIdx)->toString();
                else {
                    std::stringstream ss;
                    ss << "lit" << litIdx;
                    name = ss.str();
                }
                label += " " + name;
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

    if (node->getNodeType() == st::ControlNode::ntPhi || node->getNodeType() == st::ControlNode::ntTau)
        m_stream << "\t\t" << node->getIndex() << " [label=\"" << node->getIndex() << "\" color=\"" << color << "\"];\n";
    else
        m_stream << "\t\t" << node->getIndex() << " [shape=\"" << shape << "\" label=\"" << (node->getDomain() ? node->getDomain()->getBasicBlock()->getOffset() : 666) << "." << node->getIndex() << " : " << label << "\" color=\"" << color << "\"];\n";

    m_processedNodes[node] = true;
}

void ControlGraphVisualizer::finish() {
    m_stream << "}                         \n";
    m_stream.close();
}
