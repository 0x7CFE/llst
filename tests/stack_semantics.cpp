#include <gtest/gtest.h>
#include <instructions.h>
#include <analysis.h>
#include <visualization.h>

class StackSemanticsTemps : public ::testing::Test
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
            //81 112 245 48 33 248 15 0 83 112 245 48 246 16 0 85 178 245 241
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

TEST_F(StackSemanticsTemps, loadInCorrectBB)
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
                        EXPECT_EQ( secondArg->getNodeType(), st::ControlNode::ntInstruction );
                        st::InstructionNode* pushTemp = secondArg->cast<st::InstructionNode>();
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
