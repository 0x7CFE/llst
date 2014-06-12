#include <gtest/gtest.h>
#include <instructions.h>
#include <analysis.h>

class ABAB : public ::testing::Test
{
protected:
    TMethod* m_method;
    st::ParsedMethod* m_parsedMethod;
    st::ControlGraph* m_cfg;
    virtual void SetUp()
    {
        m_method = (new ( calloc(4, sizeof(TMethod)) ) TObject(sizeof(TMethod) / sizeof(TObject*) - 2, 0))->cast<TMethod>();
        static const uint8_t bytes[] =
        {
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
        TByteObject* bytecode = new ( calloc(4, 4096) ) TByteObject(sizeof(bytes)/sizeof(bytes[0]), static_cast<TClass*>(0));
        memcpy(bytecode->getBytes(), bytes, bytecode->getSize());
        m_method->byteCodes = bytecode;

        m_parsedMethod = new st::ParsedMethod(m_method);
        m_cfg = new st::ControlGraph(m_parsedMethod);
        m_cfg->buildGraph();
    }
    virtual void TearDown() {
        free(m_method->byteCodes);
        free(m_method);
        delete m_cfg;
        delete m_parsedMethod;
    }
};

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

TEST_F(ABAB, main)
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
