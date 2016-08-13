
#include <visualization.h>
#include <unistd.h>
#include <sys/stat.h>
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

void gnu_mkdir(const std::string& path) {
    int status = mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (status != 0) {
        std::stringstream ss;
        ss << "Cannot create '" << path << "'";
        throw std::ios_base::failure( ss.str() );
    }
}

bool gnu_dir_exists(const std::string& path) {
    struct stat info;
    int status = stat(path.c_str(), &info);
    if (status != 0)
        return false;
    if (info.st_mode & S_IFDIR)
        return true;
    return false;
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
    if (!gnu_dir_exists(directory)) {
        gnu_mkdir(directory);
    }
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
    const st::InstructionNode* const toInstruction   = to->cast<st::InstructionNode>();

    if (from->getNodeType() == st::ControlNode::ntPhi && to->getNodeType() == st::ControlNode::ntPhi)
        return "[style=invis color=red constraint=false]";

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
        if (isNodeProcessed(*iEdge) || (*iEdge)->getNodeType() == st::ControlNode::ntPhi)
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
        }

        if (const st::BranchNode* const branch = node.cast<st::BranchNode>()) {
            m_stream
                << "\t\t" << node.getIndex() << " -> " << branch->getTargetNode()->getIndex() << " ["
                << (branch->getSkipNode() ? " label=target " : "")
                << "weight=20 labelfloat=true color=\"grey\" fontcolor=\"grey\" style=\"dashed\"];\n";

            if (branch->getSkipNode()) {
                m_stream
                    << "\t\t" << node.getIndex() << " -> " << branch->getSkipNode()->getIndex() << " ["
                    << "weight=20 label=skip labelfloat=true color=\"grey\" fontcolor=\"grey\" style=\"dashed\"];\n";
            }

            outEdgesProcessed = true;
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
        }
    } else if (const st::TauNode* const tau = node.cast<st::TauNode>()) {

        for (st::TauNode::TIncomingMap::const_iterator iNode = tau->getIncomingMap().begin();
             iNode != tau->getIncomingMap().end();
             ++iNode)
        {
            if (tau->getKind() == st::TauNode::tkProvider) {
                m_stream << "\t\t" << iNode->first->getIndex() << " -> " << tau->getIndex() << " ["
                    << "weight=15 dir=back labelfloat=true color=\"red\" fontcolor=\"red\" style=\"dashed\" constraint=true ];\n";
//             } else if (tau->getKind() == st::TauNode::tkClosure) {
//                 m_stream << "\t\t" << iNode->first->getIndex() << " -> " << tau->getIndex() << " ["
//                     << "weight=15 dir=back labelfloat=true color=\"orange\" fontcolor=\"orange\" style=\"dashed\" constraint=true ];\n";
            } else {
                const bool byBackEdge = iNode->second;
                m_stream << "\t\t" << iNode->first->getIndex() << " -> " << tau->getIndex() << " ["
                    << "weight=5 dir=back labelfloat=true color=\""
                    << (byBackEdge ? "blue" : "grey")
                    <<  "\" fontcolor=\"green\" style=\"dotted\" constraint=true ];\n";
            }
        }

        for (st::TNodeSet::iterator iNode = tau->getConsumers().begin(); iNode != tau->getConsumers().end(); ++iNode) {
            if ((*iNode)->getNodeType() == st::ControlNode::ntTau)
                continue;

            if (tau->getKind() == st::TauNode::tkClosure) {
                if (static_cast<const st::ClosureTauNode*>(tau)->getOrigin() == *iNode) {
                    m_stream << "\t\t";

                    if (tau->getIncomingMap().empty())
                        m_stream  << (*iNode)->getIndex() << " -> " << tau->getIndex();
                    else
                        m_stream  << tau->getIndex() << " -> " << (*iNode)->getIndex();

                    m_stream << " [ weight=25 dir=back labelfloat=true color=\"orange\" fontcolor=\"orange\" style=\"dashed\" constraint=true ];\n";

                    continue;
                }
            }

            m_stream
                << "\t\t" << tau->getIndex() << " -> " << (*iNode)->getIndex() << " ["
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

            switch (node->cast<st::TauNode>()->getKind()) {
                case st::TauNode::tkProvider: color = "red"; break;
                case st::TauNode::tkClosure: color = "orange"; break;

                case st::TauNode::tkAggregator:
                default:
                    color = "green"; break;
                    break;
            }

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
