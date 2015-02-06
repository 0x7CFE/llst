#ifndef LLST_HELPER_CONTROL_GRAPH_INCLUDED
#define LLST_HELPER_CONTROL_GRAPH_INCLUDED

#include <gtest/gtest.h>
#include <instructions.h>
#include <analysis.h>

#include <algorithm>

class H_LastInstIsTerminator: public st::BasicBlockVisitor
{
public:
    H_LastInstIsTerminator(st::ParsedBytecode* parsedBytecode) : st::BasicBlockVisitor(parsedBytecode) {}
    virtual bool visitBlock(st::BasicBlock& BB) {
        std::size_t bbSize = BB.size();
        if (bbSize == 0)
            return true;

        st::TSmalltalkInstruction terminator(0);
        bool hasTerminator = BB.getTerminator(terminator);
        {
            SCOPED_TRACE("Each BB must have a terminator");
            EXPECT_TRUE(hasTerminator);
        }
        {
            SCOPED_TRACE("The instruction returned by BB::getTerminator must be a terminator");
            EXPECT_TRUE( terminator.isTerminator() );
        }
        {
            SCOPED_TRACE("The last instruction must be a terminator and it must be equal to BB::getTerminator");
            st::TSmalltalkInstruction lastInst = BB[bbSize-1];
            EXPECT_TRUE( lastInst.isTerminator() );
            EXPECT_EQ( lastInst.serialize(), terminator.serialize() );
        }
        {
            SCOPED_TRACE("There must be no terminators but the last one");
            int terminatorsCount = 0;
            st::BasicBlock::iterator iInstruction = BB.begin();
            st::BasicBlock::iterator iEnd         = BB.end()-1;

            while (iInstruction != iEnd) {
                bool isTerminator = (*iInstruction).isTerminator();
                if (isTerminator)
                    terminatorsCount++;
                EXPECT_FALSE(isTerminator);
                ++iInstruction;
            }
            EXPECT_EQ(0, terminatorsCount);
        }

        return true;
    }
};

class H_DomainHasTerminator: public st::DomainVisitor
{
public:
    H_DomainHasTerminator(st::ControlGraph* graph) : st::DomainVisitor(graph) {}
    virtual bool visitDomain(st::ControlDomain& domain) {
        st::InstructionNode* terminator = domain.getTerminator();
        {
            SCOPED_TRACE("Each domain must have a terminator");
            if (!terminator)
                EXPECT_TRUE( terminator != NULL );
            else
                EXPECT_TRUE( terminator->getInstruction().isTerminator() );
        }
        return true;
    }
};

class H_AreBBsLinked: public st::NodeVisitor
{
public:
    H_AreBBsLinked(st::ControlGraph* graph) : st::NodeVisitor(graph) {}
    virtual bool visitNode(st::ControlNode& node) {
        st::BasicBlock* currentBB = node.getDomain()->getBasicBlock();
        if (st::InstructionNode* inst = node.cast<st::InstructionNode>()) {
            if (inst->getInstruction().isBranch()) {
                st::TSmalltalkInstruction branch = inst->getInstruction();
                const st::TNodeSet& outEdges = node.getOutEdges();
                if (branch.getArgument() == special::branchIfTrue || branch.getArgument() == special::branchIfFalse) {
                    EXPECT_EQ(outEdges.size(), 2);
                    if (outEdges.size() == 2) {
                        st::TNodeSet::const_iterator edgeIter = outEdges.begin();
                        st::ControlNode* target1 = *(edgeIter++);
                        st::ControlNode* target2 = *edgeIter;
                        st::ControlDomain* target1Domain = target1->getDomain();
                        st::ControlDomain* target2Domain = target2->getDomain();
                        EXPECT_EQ(target1, target1Domain->getEntryPoint());
                        EXPECT_EQ(target2, target2Domain->getEntryPoint());
                        uint16_t target1Offset = target1Domain->getBasicBlock()->getOffset();
                        uint16_t target2Offset = target2Domain->getBasicBlock()->getOffset();

                        bool pointToFirst = target1Offset == branch.getExtra();
                        bool pointToSecond = target2Offset == branch.getExtra();
                        EXPECT_TRUE( pointToFirst || pointToSecond ) << "branchIf* must point to one of the targets";
                        EXPECT_FALSE( pointToFirst && pointToSecond ) << "branchIf* must point to only one of the targets";

                        {
                            SCOPED_TRACE("The referrers of outgoing edges must contain current BB");
                            st::BasicBlock::TBasicBlockSet& referrers1 = target1->getDomain()->getBasicBlock()->getReferers();
                            EXPECT_NE(referrers1.end(), referrers1.find(currentBB));
                            st::BasicBlock::TBasicBlockSet& referrers2 = target2->getDomain()->getBasicBlock()->getReferers();
                            EXPECT_NE(referrers2.end(), referrers2.find(currentBB));
                        }
                    }
                }
                if (branch.getArgument() == special::branch) {
                    EXPECT_EQ(outEdges.size(), 1);
                    if (outEdges.size() == 1) {
                        st::ControlNode* target = *(outEdges.begin());
                        st::ControlDomain* targetDomain = target->getDomain();
                        EXPECT_EQ(target, targetDomain->getEntryPoint());
                        uint16_t targetOffset = targetDomain->getBasicBlock()->getOffset();
                        EXPECT_EQ(targetOffset, branch.getExtra())
                            << "Unconditional branch must point exactly to its the only one out edge";
                        st::BasicBlock::TBasicBlockSet& referrers = target->getDomain()->getBasicBlock()->getReferers();
                        EXPECT_NE(referrers.end(), referrers.find(currentBB));
                    }
                }
            }
        }
        return true;
    }
};

class H_CorrectNumOfEdges: public st::PlainNodeVisitor
{
public:
    H_CorrectNumOfEdges(st::ControlGraph* graph) : st::PlainNodeVisitor(graph) {}
    virtual bool visitNode(st::ControlNode& node) {
        if (st::InstructionNode* inst = node.cast<st::InstructionNode>())
        {
            SCOPED_TRACE(inst->getInstruction().toString());
            switch (inst->getInstruction().getOpcode()) {
                case opcode::pushInstance:
                case opcode::pushArgument:
                case opcode::pushTemporary:
                case opcode::pushLiteral:
                case opcode::pushConstant:
                case opcode::pushBlock:
                    EXPECT_EQ( inst->getArgumentsCount(), 0);
                    break;
                case opcode::sendUnary:
                case opcode::assignInstance:
                case opcode::assignTemporary:
                    EXPECT_EQ( inst->getArgumentsCount(), 1);
                    break;
                case opcode::sendBinary:
                    EXPECT_EQ( inst->getArgumentsCount(), 2);
                    break;
                case opcode::doSpecial: {
                    switch (inst->getInstruction().getArgument()) {
                        case special::stackReturn:
                        case special::blockReturn:
                        case special::popTop:
                        case special::branchIfTrue:
                        case special::branchIfFalse:
                        case special::duplicate:
                            EXPECT_EQ( inst->getArgumentsCount(), 1);
                            break;
                        case special::branch:
                            EXPECT_EQ( inst->getArgumentsCount(), 0);
                            break;
                        case special::sendToSuper:
                            EXPECT_EQ( inst->getArgumentsCount(), 1);
                            break;
                    }
                } break;
                case opcode::doPrimitive:
                    EXPECT_EQ(inst->getInstruction().getArgument(), inst->getArgumentsCount());
                    break;
                default:
                    EXPECT_GE( inst->getArgumentsCount(), 1);
                    break;
            }
            if (inst->getInstruction().isValueProvider()) {
                const st::TNodeSet& consumers = inst->getConsumers();
                EXPECT_GT(consumers.size(), 0);
            }
        }
        if (st::PhiNode* phi = node.cast<st::PhiNode>()) {
            const st::TNodeSet& inEdges = phi->getInEdges();
            const st::TNodeSet& outEdges = phi->getOutEdges();
            EXPECT_GT(inEdges.size(), 0) << "The phi must consist of at least 1 incoming edge";
            EXPECT_GE(outEdges.size(), 1) << "There must be a node using the given phi";
        }
        if (st::TauNode* tau = node.cast<st::TauNode>()) {
            EXPECT_TRUE(tau == NULL /* always fails */); // TODO
        }
        return true;
    }
};

class H_NoOrphans {
    st::ControlGraph* m_cfg;
public:
    H_NoOrphans(st::ControlGraph* graph) : m_cfg(graph) {}
    void check() {
        st::TNodeSet linkedNodes = getLinkedNodes();
        st::TNodeSet allNodes = getAllNodes();
        typedef std::vector<st::ControlNode*> TOrphans;
        TOrphans orphans;
        std::set_difference(
            linkedNodes.begin(), linkedNodes.end(),
            allNodes.begin(), allNodes.end(),
            std::back_inserter(orphans)
        );
        for (TOrphans::const_iterator it = orphans.begin(); it != orphans.end(); ++it) {
            st::ControlNode* node = *it;
            std::stringstream ss;
            ss << "Orphan node index: " << node->getIndex()
               << " from BB offset: " << node->getDomain()->getBasicBlock()->getOffset();
            FAIL() << ss.str();
        }
    }
private:
    st::TNodeSet getLinkedNodes() const {
        class LinkedNodesGetter : public st::NodeVisitor {
        public:
            st::TNodeSet& m_visitedNodes;
            LinkedNodesGetter(st::ControlGraph* graph, st::TNodeSet& outResult)
                : st::NodeVisitor(graph), m_visitedNodes(outResult) {}
            virtual bool visitNode(st::ControlNode& node) {
                m_visitedNodes.insert(&node);
                st::TNodeSet outEdges = node.getOutEdges();
                for(st::TNodeSet::const_iterator it = outEdges.begin(); it != outEdges.end(); ++it) {
                    m_visitedNodes.insert(*it);
                }
                return true;
            }
        };
        st::TNodeSet result;
        LinkedNodesGetter getter(m_cfg, result);
        getter.run();
        return result;
    }
    st::TNodeSet getAllNodes() const {
        class AllNodesGetter : public st::PlainNodeVisitor {
        public:
            st::TNodeSet& m_visitedNodes;
            AllNodesGetter(st::ControlGraph* graph, st::TNodeSet& outResult)
                : st::PlainNodeVisitor(graph), m_visitedNodes(outResult) {}
            virtual bool visitNode(st::ControlNode& node) {
                m_visitedNodes.insert(&node);
                return true;
            }
        };
        st::TNodeSet result;
        AllNodesGetter getter(m_cfg, result);
        getter.run();
        return result;
    }
};


void H_CheckCFGCorrect(st::ControlGraph* graph)
{
    {
        H_LastInstIsTerminator visitor(graph->getParsedMethod());
        visitor.run();
    }
    {
        H_DomainHasTerminator visitor(graph);
        visitor.run();
    }
    {
        H_AreBBsLinked visitor(graph);
        visitor.run();
    }
    {
        H_CorrectNumOfEdges visitor(graph);
        visitor.run();
    }
    {
        H_NoOrphans checker(graph);
        checker.check();
    }
}

#endif
