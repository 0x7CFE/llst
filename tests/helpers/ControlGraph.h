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

#endif
