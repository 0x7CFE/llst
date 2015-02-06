#ifndef LLST_HELPER_CONTROL_GRAPH_INCLUDED
#define LLST_HELPER_CONTROL_GRAPH_INCLUDED

#include <gtest/gtest.h>
#include <instructions.h>
#include <analysis.h>

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

class H_AreBBsLinked: public st::DomainVisitor
{
public:
    H_AreBBsLinked(st::ControlGraph* graph) : st::DomainVisitor(graph) {}
    virtual bool visitDomain(st::ControlDomain& domain) {
        st::BasicBlock::TBasicBlockSet referrers = domain.getBasicBlock()->getReferers();

        for(st::BasicBlock::TBasicBlockSet::const_iterator referrer = referrers.begin(); referrer != referrers.end(); ++referrer) {
            st::TSmalltalkInstruction terminator(0);
            bool referrerHasTerminator = (*referrer)->getTerminator(terminator);

            EXPECT_TRUE(referrerHasTerminator);
            EXPECT_TRUE(terminator.isBranch());
            EXPECT_EQ( terminator.getExtra(), domain.getBasicBlock()->getOffset() )
                << "The destination of terminator must be the offset of the first instruction of BB";
        }
        return true;
    }
};

class H_CorrectNumOfEdges: public st::NodeVisitor
{
public:
    H_CorrectNumOfEdges(st::ControlGraph* graph) : st::NodeVisitor(graph) {}
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
            EXPECT_GT(inEdges.size(), 0);
            EXPECT_GT(outEdges.size(), 0);
        }
        if (st::TauNode* tau = node.cast<st::TauNode>()) {
            EXPECT_TRUE(tau == NULL /* always fails */); // TODO
        }
        return true;
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
}

#endif
