#include <gtest/gtest.h>
#include <instructions.h>

class InstructionDecoderRealMethodTest : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        void* mem = calloc(4, 1024); // magic numbers
        m_bytecode = new (mem) TByteObject(42, static_cast<TClass*>(0));
        static const uint8_t bytes[] =
        {
            // This is the bytecode for method Block>>assertEq:withComment:

            64,     // PushLiteral 0
            129,    // MarkArguments 1
            145,    // SendMessage new
            113,    // AssignTemporary 1
            245,    // DoSpecial popTop
            49,     // PushTemporary 1
            16,     // PushInstance 0
            129,    // MarkArguments 1
            146,    // SendMessage name
            129,    // MarkArguments 1
            147,    // SendMessage printString
            178,    // SendBinary +
            113,    // AssignTemporary 1
            245,    // DoSpecial popTop
            34,     // PushArgument 2
            161,    // SendUnary notNil
            248,    //// DoSpecial branchIfFalse 30
            30,     ///
            0,      //
            49,     // PushTemporary 1
            68,     // PushLiteral 4
            178,    // SendBinary +
            34,     // PushArgument 2
            178,    // SendBinary +
            69,     // PushLiteral 5
            178,    // SendBinary +
            113,    // AssignTemporary 1
            246,    //// DoSpecial branch 31
            31,     ///
            0,      //
            90,     // PushConstant nil
            245,    // DoSpecial popTop
            49,     // PushTemporary 1
            129,    // MarkArguments 1
            150,    // SendMessage print
            245,    // DoSpecial popTop
            71,     // PushLiteral 7
            49,     // PushTemporary 1
            129,    // MarkArguments 1
            152,    // SendMessage size
            130,    // MarkArguments 2
            153     // SendMessage -
        };
        memcpy(m_bytecode->getBytes(), bytes, sizeof(bytes)/sizeof(bytes[0]));
    }
    virtual void TearDown() {
        free(m_bytecode);
    }
    TByteObject* m_bytecode;
};

void encodeInstruction(const st::TSmalltalkInstruction& inst, uint8_t* bytes, uint16_t& bytePointer)
{
    if (inst.getArgument() <= 0x0F) {
        bytes[bytePointer++] = (inst.getOpcode() << 4) | inst.getArgument();
    } else {
        bytes[bytePointer++] = inst.getOpcode();
        bytes[bytePointer++] = inst.getArgument();
    }
    switch(inst.getOpcode())
    {
        case opcode::doPrimitive:
            bytes[bytePointer++] = inst.getExtra();
            return;
        case opcode::pushBlock:
            bytes[bytePointer++] = inst.getExtra();
            bytes[bytePointer++] = inst.getExtra() >> 8;
            return;
        case opcode::doSpecial:
            switch (inst.getArgument()) {
                case special::branch:
                case special::branchIfTrue:
                case special::branchIfFalse:
                    bytes[bytePointer++] = inst.getExtra();
                    bytes[bytePointer++] = inst.getExtra() >> 8;
                    return;

                case special::sendToSuper:
                    bytes[bytePointer++] = inst.getExtra();
                    return;
            }
        default: return;
    }
}

class InstructionDecoderSyntheticOpcodesTest : public ::testing::Test
{
protected:
    virtual void SetUp() {
        const st::TSmalltalkInstruction::TArgument minArgument = std::numeric_limits<st::TSmalltalkInstruction::TArgument>::min();
        const st::TSmalltalkInstruction::TArgument maxArgument = std::numeric_limits<st::TSmalltalkInstruction::TArgument>::max();

        const st::TSmalltalkInstruction::TExtra minExtra = std::numeric_limits<st::TSmalltalkInstruction::TExtra>::min();
        const st::TSmalltalkInstruction::TExtra maxExtra = std::numeric_limits<st::TSmalltalkInstruction::TExtra>::max();

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushInstance, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushInstance, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushArgument, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushArgument, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushTemporary, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushTemporary, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushLiteral, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushLiteral, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushConstant, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushConstant, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::assignInstance, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::assignInstance, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::assignTemporary, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::assignTemporary, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::markArguments, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::markArguments, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::sendMessage, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::sendMessage, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::sendUnary, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::sendUnary, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::sendBinary, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::sendBinary, maxArgument).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushBlock, minArgument, minExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushBlock, minArgument, maxExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushBlock, maxArgument, minExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::pushBlock, maxArgument, maxExtra).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doPrimitive, minArgument, minExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doPrimitive, minArgument, std::numeric_limits<uint8_t>::max()).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doPrimitive, maxArgument, minExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doPrimitive, maxArgument, std::numeric_limits<uint8_t>::max()).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, special::branch, minExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, special::branch, maxExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse, minExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfFalse, maxExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue, minExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, special::branchIfTrue, maxExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, special::sendToSuper, minExtra).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, special::sendToSuper, std::numeric_limits<uint8_t>::max()).serialize() );

        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, minArgument).serialize() );
        m_instructions.push_back( st::TSmalltalkInstruction(opcode::doSpecial, maxArgument).serialize() );

        void* mem = calloc(4, 1024); // magic numbers
        m_bytecode = new (mem) TByteObject(420 /*another magic number*/, static_cast<TClass*>(0));
        uint16_t bytePointer = 0;
        uint8_t* bytes = m_bytecode->getBytes();

        for(std::size_t i = 0; i < m_instructions.size(); i++) {
            st::TSmalltalkInstruction x( m_instructions[i] );
            encodeInstruction(x, bytes, bytePointer);
        }
    }
    virtual void TearDown() {
        free(m_bytecode);
    }
    std::vector<uint32_t> m_instructions;
    TByteObject* m_bytecode;
};

TEST_F(InstructionDecoderRealMethodTest, bytePointerIsShifted)
{
    uint16_t bytePointerLastValue = 0;
    uint16_t bytePointer = bytePointerLastValue;

    while( bytePointer < m_bytecode->getSize() ) {
        SCOPED_TRACE(bytePointer);
        st::InstructionDecoder::decodeAndShiftPointer(*m_bytecode, bytePointer);
        ASSERT_NE(bytePointer, bytePointerLastValue);
        bytePointerLastValue = bytePointer;
    }
}

TEST_F(InstructionDecoderSyntheticOpcodesTest, decodeAndShiftPointer)
{
    uint16_t bytePointer = 0;
    for(std::size_t i = 0; i < m_instructions.size(); i++) {
        SCOPED_TRACE(i);
        st::TSmalltalkInstruction x = st::InstructionDecoder::decodeAndShiftPointer(*m_bytecode, bytePointer);
        st::TSmalltalkInstruction y = st::TSmalltalkInstruction( m_instructions[i] );
        EXPECT_EQ(x.getOpcode(), y.getOpcode());
        EXPECT_EQ(x.getArgument(), y.getArgument());
        EXPECT_EQ(x.getExtra(), y.getExtra());
    }
}
