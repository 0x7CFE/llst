#include "patterns/DecodeBytecode.h"

static const uint8_t bytecode[] = {
    81,         // 0000 PushConstant 1
    112,        // 0001 AssignTemporary 0
    245,        // 0002 DoSpecial popTop
    48,         // 0003 PushTemporary 0
    33,         // 0004 PushArgument 1
    248,        // 0005 DoSpecial branchIfFalse 15
    15,         //
    0,          //
    83,         // 0008 PushConstant 3
    112,        // 0009 AssignTemporary 0
    245,        // 0010 DoSpecial popTop
    48,         // 0011 PushTemporary 0
    246,        // 0012 DoSpecial branch 16
    16,         //
    0,          //
    85,         // 0015 PushConstant 5
    178         // 0016 SendBinary +
};

INSTANTIATE_TEST_CASE_P(_, P_DecodeBytecode,
    ::testing::Values( std::tr1::make_tuple(std::string("Bytecode"), std::string(reinterpret_cast<const char*>(bytecode), sizeof(bytecode))) )
);

TEST_P(P_DecodeBytecode, StackSemanticsTemps)
{
    class TempsLoadInCorrectBB: public st::NodeVisitor
    {
    public:
        TempsLoadInCorrectBB(st::ControlGraph* graph) : st::NodeVisitor(graph) {}
        virtual bool visitNode(st::ControlNode& node) {
            if ( st::InstructionNode* inst = node.cast<st::InstructionNode>() )
            {
                if (inst->getInstruction().getOpcode() == opcode::sendBinary)
                {
                    st::ControlNode* firstArg = inst->getArgument(0);
                    st::ControlNode* secondArg = inst->getArgument(1);
                    EXPECT_NE(firstArg, secondArg);

                    {
                        SCOPED_TRACE("Second arg must be phi");
                        EXPECT_EQ( secondArg->getNodeType(), st::ControlNode::ntPhi );
                    }
                    {
                        SCOPED_TRACE("First arg must be PushTemporary");
                        EXPECT_EQ( firstArg->getNodeType(), st::ControlNode::ntInstruction );
                        st::InstructionNode* pushTemp = firstArg->cast<st::InstructionNode>();
                        EXPECT_TRUE(pushTemp != NULL);
                        if (pushTemp)
                            EXPECT_EQ(pushTemp->getInstruction().getOpcode(), opcode::pushTemporary);
                    }

                    return false;
                }
            }
            return true;
        }
    };
    TempsLoadInCorrectBB test(m_cfg);
    test.run();
}
