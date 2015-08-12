#ifndef LLST_INSTRUCTIONS_INCLUDED
#define LLST_INSTRUCTIONS_INCLUDED

#include <assert.h>
#include <stdint.h>
#include <vector>
#include <list>
#include <map>
#include <set>

#include <types.h>
#include <opcodes.h>

namespace st {

struct TSmalltalkInstruction {
    typedef opcode::Opcode TOpcode;
    typedef uint8_t        TArgument;
    typedef uint16_t       TExtra;
    typedef uint32_t       TUnpackedBytecode;

    TSmalltalkInstruction(TOpcode opcode, TArgument argument = 0, TExtra extra = 0)
        : m_opcode(opcode), m_argument(argument), m_extra(extra) {}

    // Initialize instruction from the unpacked value
    TSmalltalkInstruction(TUnpackedBytecode bytecode) {
        m_opcode   = static_cast<TOpcode>(bytecode & 0xFF);
        m_argument = static_cast<TArgument>(bytecode >> 8);
        m_extra    = static_cast<TExtra>(bytecode >> 16);
    }

    TOpcode getOpcode() const { return m_opcode; }
    TArgument getArgument() const { return m_argument; }
    TExtra getExtra() const { return m_extra; }

    // Return fixed width representation of bytecode suitable for storing in arrays
    TUnpackedBytecode serialize() const {
        return
            static_cast<TUnpackedBytecode>(m_opcode) |
            static_cast<TUnpackedBytecode>(m_argument) << 8 |
            static_cast<TUnpackedBytecode>(m_extra) << 16;
    }

    bool operator ==(const TSmalltalkInstruction& instruction) const {
        return
            m_opcode   == instruction.m_opcode &&
            m_argument == instruction.m_argument &&
            m_extra    == instruction.m_extra;
    }

    bool isTrivial() const;
    bool isTerminator() const;
    bool isBranch() const;
    bool isValueProvider() const;
    bool isValueConsumer() const;
    bool mayCauseGC() const;

    std::string toString() const;

private:
    TOpcode   m_opcode;
    TArgument m_argument;
    TExtra    m_extra;
};

class InstructionDecoder {
public:
    InstructionDecoder(const TByteObject& byteCodes, uint16_t bytePointer = 0)
        : m_byteCodes(byteCodes), m_bytePointer(bytePointer) {}

    uint16_t getBytePointer() const { return m_bytePointer; }
    void setBytePointer(uint16_t value) {
        assert(value < m_byteCodes.getSize());
        m_bytePointer = value;
    }

    const TSmalltalkInstruction decodeAndShiftPointer() {
        return decodeAndShiftPointer(m_byteCodes, m_bytePointer);
    }

    static const TSmalltalkInstruction decodeAndShiftPointer(const TByteObject& byteCodes, uint16_t& bytePointer);

private:
    const TByteObject& m_byteCodes;
    uint16_t m_bytePointer;
};

} // namespace st

#endif
