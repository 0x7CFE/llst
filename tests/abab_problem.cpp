#include "patterns/DecodeBytecode.h"

static const uint8_t ABABbytecode[] = {
    33,         // 0000 PushArgument 1
    248,        // 0001 DoSpecial branchIfFalse 8
    8,
    0,
    81,         // 0004 PushConstant 1
    246,        // 0005 DoSpecial branch 9
    9,
    0,
    83,         // 0008 PushConstant 3
    34,         // 0009 PushArgument 2
    248,        // 0010 DoSpecial branchIfFalse 17
    17,
    0,
    85,         // 0013 PushConstant 5
    246,        // 0014 DoSpecial branch 18
    18,
    0,
    87,         // 0017 PushConstant 7
    178         // 0018 SendBinary +
};

INSTANTIATE_TEST_CASE_P(
    testABAB,
    P_DecodeBytecode,
    ::testing::Values( std::tr1::make_tuple(std::string("Bytecode for ABAB"), std::string(reinterpret_cast<const char*>(ABABbytecode), sizeof(ABABbytecode))) )
);

void checkSendBinaryArg(st::ControlNode* arg)
{
    {
        SCOPED_TRACE("Each argument of sendBinary is a phi node");
        EXPECT_EQ( arg->getNodeType(), st::ControlNode::ntPhi);
    }
    {
        SCOPED_TRACE("Each argument of sendBinary has 2 egdes");
        EXPECT_EQ( arg->getInEdges().size(), 2);
    }
    {
        SCOPED_TRACE("Each edge of arg-phi is a PushConstant");
        for(st::TNodeSet::iterator i = arg->getInEdges().begin(); i != arg->getInEdges().end(); i++) {
            st::ControlNode* edge = *i;
            EXPECT_EQ( edge->getNodeType(), st::ControlNode::ntInstruction );
            if (st::InstructionNode* edgeInst = edge->cast<st::InstructionNode>()) {
                EXPECT_EQ( edgeInst->getInstruction().getOpcode(), opcode::pushConstant );
            }
        }
    }
}

TEST_P(P_DecodeBytecode, ABAB)
{
    class ABABProblem: public st::NodeVisitor
    {
    public:
        bool sendBinaryFound;
        ABABProblem(st::ControlGraph* graph) : st::NodeVisitor(graph), sendBinaryFound(false) {}
        virtual bool visitNode(st::ControlNode& node) {
            if ( st::InstructionNode* inst = node.cast<st::InstructionNode>() )
            {
                if (inst->getInstruction().getOpcode() == opcode::sendBinary)
                {
                    sendBinaryFound = true;

                    EXPECT_EQ( inst->getInEdges().size(), 4); // 2 branches + 2 phis
                    EXPECT_EQ( inst->getArgumentsCount(), 2);
                    st::ControlNode* firstArg = inst->getArgument(0);
                    st::ControlNode* secondArg = inst->getArgument(1);
                    EXPECT_NE(firstArg, secondArg);

                    {
                        SCOPED_TRACE("Check first arg");
                        checkSendBinaryArg(firstArg);
                    }
                    {
                        SCOPED_TRACE("Check second arg");
                        checkSendBinaryArg(secondArg);
                    }

                    return false;
                }
            }
            return true;
        }
    };
    ABABProblem abab(m_cfg);
    abab.run();
    EXPECT_TRUE(abab.sendBinaryFound);
}
