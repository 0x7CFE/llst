#include <gtest/gtest.h>
#include <instructions.h>

TEST(TSmalltalkInstruction, isTerminator)
{
    {
        SCOPED_TRACE("branches are terminators");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branch).isTerminator());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue).isTerminator());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse).isTerminator());
    }
    {
        SCOPED_TRACE("returns are terminators");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::stackReturn).isTerminator());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::selfReturn).isTerminator());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::blockReturn).isTerminator());
    }
}

TEST(TSmalltalkInstruction, isBranch)
{
    EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branch).isBranch());
    EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue).isBranch());
    EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse).isBranch());
}

TEST(TSmalltalkInstruction, isValueProvider)
{
    {
        SCOPED_TRACE("branches are not providers");
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branch).isValueProvider());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue).isValueProvider());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse).isValueProvider());
    }
    {
        SCOPED_TRACE("returns are not providers");
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::stackReturn).isValueProvider());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::selfReturn).isValueProvider());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::blockReturn).isValueProvider());
    }
    {
        SCOPED_TRACE("some other insts are not providers either");
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::assignTemporary).isValueProvider());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::assignInstance).isValueProvider());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doPrimitive).isValueProvider());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::popTop).isValueProvider());
    }
}

TEST(TSmalltalkInstruction, isValueConsumer)
{
    {
        SCOPED_TRACE("conditional branches are consumers");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse).isValueConsumer());
    }
    {
        SCOPED_TRACE("insts dealing with messages are consumers");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::markArguments).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::sendMessage).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::sendUnary).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::sendBinary).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::sendToSuper).isValueConsumer());
    }
    {
        SCOPED_TRACE("stack readers are consumers");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::assignTemporary).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::assignInstance).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doPrimitive).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::duplicate).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::popTop).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::stackReturn).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::blockReturn).isValueConsumer());
    }
}

TEST(TSmalltalkInstruction, serializeIsInverseToCtor)
{
    using namespace opcode;
    static const Opcode opcodes[] =
    {
        extended,
        pushInstance,
        pushArgument,
        pushTemporary,
        pushLiteral,
        pushConstant,
        assignInstance,
        assignTemporary,
        markArguments,
        sendMessage,
        sendUnary,
        sendBinary,
        pushBlock,
        doPrimitive,
        doSpecial
    };
    for(std::size_t i = 0; i < sizeof(opcodes) / sizeof(opcodes[0]); i++)
    {
        SCOPED_TRACE(i);
        const Opcode currentOpcode = opcodes[i];

        const st::TSmalltalkInstruction::TArgument minArgument = std::numeric_limits<st::TSmalltalkInstruction::TArgument>::min();
        const st::TSmalltalkInstruction::TArgument maxArgument = std::numeric_limits<st::TSmalltalkInstruction::TArgument>::max();

        const st::TSmalltalkInstruction::TExtra minExtra = std::numeric_limits<st::TSmalltalkInstruction::TExtra>::min();
        const st::TSmalltalkInstruction::TExtra maxExtra = std::numeric_limits<st::TSmalltalkInstruction::TExtra>::max();

        {
            SCOPED_TRACE("minArgument;minExtra");
            st::TSmalltalkInstruction x( currentOpcode, minArgument, minExtra );
            st::TSmalltalkInstruction y( x.serialize() );
            EXPECT_EQ(x.getOpcode(), y.getOpcode());
            EXPECT_EQ(x.getArgument(), y.getArgument());
            EXPECT_EQ(x.getExtra(), y.getExtra());
        }
        {
            SCOPED_TRACE("maxArgument;minExtra");
            st::TSmalltalkInstruction x( currentOpcode, maxArgument, minExtra );
            st::TSmalltalkInstruction y( x.serialize() );
            EXPECT_EQ(x.getOpcode(), y.getOpcode());
            EXPECT_EQ(x.getArgument(), y.getArgument());
            EXPECT_EQ(x.getExtra(), y.getExtra());
        }
        {
            SCOPED_TRACE("minArgument;maxExtra");
            st::TSmalltalkInstruction x( currentOpcode, minArgument, maxExtra);
            st::TSmalltalkInstruction y( x.serialize() );
            EXPECT_EQ(x.getOpcode(), y.getOpcode());
            EXPECT_EQ(x.getArgument(), y.getArgument());
            EXPECT_EQ(x.getExtra(), y.getExtra());
        }
        {
            SCOPED_TRACE("maxArgument;maxExtra");
            st::TSmalltalkInstruction x( currentOpcode, maxArgument, maxExtra );
            st::TSmalltalkInstruction y( x.serialize() );
            EXPECT_EQ(x.getOpcode(), y.getOpcode());
            EXPECT_EQ(x.getArgument(), y.getArgument());
            EXPECT_EQ(x.getExtra(), y.getExtra());
        }
    }
}
