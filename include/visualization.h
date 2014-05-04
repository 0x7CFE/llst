#ifndef LLST_VISUALIZATION_H_INCLUDED
#define LLST_VISUALIZATION_H_INCLUDED

#include <map>
#include <analysis.h>

#include <../ogdf/ogdf/basic/Graph.h>
#include <../ogdf/ogdf/basic/GraphAttributes.h>

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

private:
    ogdf::Graph* m_ogdfGraph;
    ogdf::GraphAttributes* m_ogdfAttributes;

    typedef std::map<st::ControlNode*, ogdf::NodeElement*> TNodeMap;
    TNodeMap m_nodes;
};

#endif
