#ifndef LLST_VISUALIZATION_H_INCLUDED
#define LLST_VISUALIZATION_H_INCLUDED

#include <map>
#include <analysis.h>

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>

class ControlGraphVisualizer : public st::NodeVisitor {
public:
    ControlGraphVisualizer(st::ControlGraph* graph) : st::NodeVisitor(graph) { m_ogdfGraph = new ogdf::Graph(); }
    virtual ~ControlGraphVisualizer() { delete m_ogdfGraph; }

    virtual bool visitDomain(st::ControlDomain& domain);
    virtual bool visitNode(st::ControlNode& node);

    void writeGraphTo(const std::string& fileName);

private:
    ogdf::NodeElement* getNodeFor(st::ControlNode*);
    void addEdge(st::ControlNode* from, st::ControlNode* to);
    void applyLayout();
    void setNodeProperties(st::ControlNode* node, ogdf::NodeElement* element);

private:
    ogdf::Graph* m_ogdfGraph;
    ogdf::GraphAttributes* m_ogdfAttributes;

    typedef std::map<st::ControlNode*, ogdf::NodeElement*> TNodeMap;
    TNodeMap m_nodes;

    uint32_t m_lastX;
    uint32_t m_lastY;
};

#endif
