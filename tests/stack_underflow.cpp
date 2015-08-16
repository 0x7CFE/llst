#include "patterns/DecodeBytecode.h"
#include "helpers/ControlGraph.h"

static const uint8_t UnderflowBytecode[] = {
    178,    // SendBinary +
    178,    // SendBinary +
    178,    // SendBinary +
    178,    // SendBinary +
    242     // DoSpecial stackReturn
};

INSTANTIATE_TEST_CASE_P(_, P_DecodeBytecode,
    ::testing::Values( std::tr1::make_tuple(
        std::string("UnderflowBytecode"),
        std::string(reinterpret_cast<const char*>(UnderflowBytecode), sizeof(UnderflowBytecode))) )
);

TEST_P(P_DecodeBytecode, Underflow) {
    // TODO: do nothing but check err msg
    H_CorrectNumOfEdges check(m_cfg);
    check.run();
}
