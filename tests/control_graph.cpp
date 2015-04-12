#include "patterns/DecodeBytecode.h"
#include "helpers/ControlGraph.h"

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
    H_LastInstIsTerminator visitor(m_cfg->getParsedMethod());
    visitor.run();
}

TEST_P(P_DecodeBytecode, eachDomainHasTerminator)
{
    H_DomainHasTerminator visitor(m_cfg);
    visitor.run();
}

TEST_P(P_DecodeBytecode, BBsAreLinkedTogether)
{
    H_AreBBsLinked visitor(m_cfg);
    visitor.run();
}
