#include "patterns/DecodeBytecode.h"

static const uint8_t bytecode[] =
{
    // This is the bytecode for method Object>>isKindOf:
    32,
    129,
    144,
    112,
    245,
    193,
    11,
    0,
    48,
    161,
    242,
    48,
    161,
    248,
    38,
    0,
    48,
    33,
    130,
    145,
    248,
    28,
    0,
    91,
    242,
    246,
    29,
    0,
    90,
    245,
    48,
    129,
    146,
    112,
    245,
    246,
    11,
    0,
    80,
    245,
    92,
    242,
    245,
    241
};

INSTANTIATE_TEST_CASE_P(_, P_DecodeBytecode,
    ::testing::Values( std::tr1::make_tuple(std::string("Object>>isKindOf:"), std::string(reinterpret_cast<const char*>(bytecode), sizeof(bytecode))) )
);

TEST_P(P_DecodeBytecode, buildGraphMoreThanOnce)
{
    st::ControlGraph cfg(m_parsedMethod);
    cfg.buildGraph();
    //cfg.buildGraph();
    EXPECT_EQ( std::distance(m_cfg->begin(), m_cfg->end()) , std::distance(cfg.begin(), cfg.end()) );
}

TEST_P(P_DecodeBytecode, lastInstIsTerminator)
{
    class lastIsTerminator: public st::BasicBlockVisitor
    {
    public:
        lastIsTerminator(st::ParsedBytecode* parsedBytecode) : st::BasicBlockVisitor(parsedBytecode) {}
        virtual bool visitBlock(st::BasicBlock& BB) {
            std::size_t bbSize = BB.size();
            EXPECT_NE(bbSize, 0);
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

            return true;
        }
    };
    lastIsTerminator visitor(m_cfg->getParsedMethod());
    visitor.run();
}

TEST_P(P_DecodeBytecode, eachDomainHasTerminator)
{
    class domainHasTerminator: public st::DomainVisitor
    {
    public:
        TMethod* m_method;
        domainHasTerminator(st::ControlGraph* graph, TMethod* method) : st::DomainVisitor(graph), m_method(method) {}
        virtual bool visitDomain(st::ControlDomain& domain) {
            st::InstructionNode* terminator = domain.getTerminator();
            {
                SCOPED_TRACE("Domain must have a terminator");
                EXPECT_NE( terminator, static_cast<st::InstructionNode*>(0) );
                EXPECT_TRUE( terminator->getInstruction().isTerminator() );
            }
            return true;
        }
    };
    domainHasTerminator visitor(m_cfg, m_method);
    visitor.run();
}

TEST_P(P_DecodeBytecode, BBsAreLinkedTogether)
{
    class areBBsLinked: public st::DomainVisitor
    {
    public:
        int& m_numberOfReferrers;
        areBBsLinked(st::ControlGraph* graph, int& numberOfReferrers) :
            st::DomainVisitor(graph),
            m_numberOfReferrers(numberOfReferrers)
        {}
        virtual bool visitDomain(st::ControlDomain& domain) {
            st::BasicBlock::TBasicBlockSet referrers = domain.getBasicBlock()->getReferers();
            m_numberOfReferrers += referrers.size();

            for(st::BasicBlock::TBasicBlockSet::const_iterator referrer = referrers.begin(); referrer != referrers.end(); ++referrer) {
                st::TSmalltalkInstruction terminator(0);
                bool referrerHasTerminator = (*referrer)->getTerminator(terminator);

                EXPECT_TRUE(referrerHasTerminator);
                EXPECT_TRUE(terminator.isBranch());
                EXPECT_EQ( terminator.getExtra(), domain.getBasicBlock()->getOffset() );
            }
            return true;
        }
    };
    int numberOfReferrers = 0;
    areBBsLinked visitor(m_cfg, numberOfReferrers);
    visitor.run();

    {
        SCOPED_TRACE("There must be at least one referrer");
        EXPECT_NE(numberOfReferrers, 0);
    }
}
