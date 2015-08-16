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
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::popTop).isValueProvider());
    }
    {
        SCOPED_TRACE("the other are providers");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doPrimitive).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::duplicate).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::markArguments).isValueProvider());

        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushInstance).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushArgument).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushTemporary).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushLiteral).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushBlock).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushConstant).isValueProvider());

        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::sendMessage).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::sendUnary).isValueProvider());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::sendBinary).isValueProvider());
    }
}

TEST(TSmalltalkInstruction, isValueConsumer)
{
    {
        SCOPED_TRACE("conditional branches are consumers");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue).isValueConsumer());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse).isValueConsumer());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branch).isValueConsumer());
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
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::selfReturn).isValueConsumer());
    }
    {
        SCOPED_TRACE("stack writers are not consumers");
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushInstance).isValueConsumer());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushArgument).isValueConsumer());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushTemporary).isValueConsumer());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushLiteral).isValueConsumer());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushBlock).isValueConsumer());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushConstant).isValueConsumer());
    }
}

TEST(TSmalltalkInstruction, isTrivial)
{
    {
        SCOPED_TRACE("trivial");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushInstance).isTrivial());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushArgument).isTrivial());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushTemporary).isTrivial());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushLiteral).isTrivial());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushConstant).isTrivial());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::duplicate).isTrivial());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::markArguments).isTrivial());
    }
    {
        SCOPED_TRACE("the other are not trivial");
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushBlock).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branch).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::popTop).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::stackReturn).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::blockReturn).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::selfReturn).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::sendToSuper).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::sendMessage).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::sendUnary).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::sendBinary).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::assignTemporary).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::assignInstance).isTrivial());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doPrimitive).isTrivial());
    }
}

TEST(TSmalltalkInstruction, mayCauseGC)
{
    {
        SCOPED_TRACE("may cause gc");
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::pushBlock).mayCauseGC());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::sendMessage).mayCauseGC());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::sendBinary).mayCauseGC());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doPrimitive).mayCauseGC());
        EXPECT_TRUE(st::TSmalltalkInstruction(opcode::doSpecial, special::sendToSuper).mayCauseGC());
    }
    {
        SCOPED_TRACE("never cause gc");
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushInstance).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushArgument).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushTemporary).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushLiteral).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::pushConstant).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::branch).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::popTop).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::stackReturn).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::blockReturn).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::selfReturn).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::doSpecial, special::duplicate).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::sendUnary).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::assignTemporary).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::assignInstance).mayCauseGC());
        EXPECT_FALSE(st::TSmalltalkInstruction(opcode::markArguments).mayCauseGC());
    }
}

TEST(TSmalltalkInstruction, toString)
{
    {
        SCOPED_TRACE("nonexistent instructions");
        EXPECT_ANY_THROW(st::TSmalltalkInstruction(42).toString());
        EXPECT_ANY_THROW(st::TSmalltalkInstruction(opcode::doSpecial, 42).toString());
        EXPECT_ANY_THROW(st::TSmalltalkInstruction(opcode::sendUnary, 42).toString());
        EXPECT_ANY_THROW(st::TSmalltalkInstruction(opcode::sendBinary, 42).toString());
        EXPECT_ANY_THROW(st::TSmalltalkInstruction(opcode::pushConstant, 42).toString());
    }
    {
        SCOPED_TRACE("existent instructions");
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::pushBlock).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::pushInstance).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::pushArgument).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::pushTemporary).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::pushLiteral).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::pushConstant).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::sendMessage).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::sendBinary).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::sendUnary).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doPrimitive).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::assignTemporary).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::assignInstance).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::markArguments).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doSpecial, special::branch).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doSpecial, special::popTop).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doSpecial, special::stackReturn).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doSpecial, special::blockReturn).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doSpecial, special::selfReturn).toString());
        EXPECT_NO_THROW(st::TSmalltalkInstruction(opcode::doSpecial, special::duplicate).toString());
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
