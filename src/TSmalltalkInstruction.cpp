#include <instructions.h>

using namespace st;

TSmalltalkInstruction::TSmalltalkInstruction(const TByteObject& byteCodes, uint16_t& bytePointer)
    : m_opcode(opcode::extended), m_argument(0), m_extra(0)
{
    const uint8_t& bytecode = byteCodes[bytePointer++];

    // For normal bytecodes higher part of the byte holds opcode
    // whether lower part holds the argument
    m_opcode   = static_cast<TOpcode>(bytecode >> 4);
    m_argument = bytecode & 0x0F;

    // Extended opcodes encode argument in a separate byte
    // Opcode is stored in a lower half of the first byte
    if (m_opcode == opcode::extended) {
        m_opcode   = static_cast<TOpcode>(m_argument);
        m_argument = byteCodes[bytePointer++];
    }

    // Some instructions hold extra data in a bytes right after instruction
    switch (m_opcode) {
        case opcode::pushBlock:
            // Storing bytecode offset as extra
            m_extra = byteCodes[bytePointer] | (byteCodes[bytePointer+1] << 8);
            bytePointer += 2;
            break;

        case opcode::doPrimitive:
            // Primitive number do not fit into lower 4 bits of opcode byte.
            // So it is stored in a separate byte right after. Technically,
            // this value is an argument for instruction so it would be logical
            // to hold it in the argument field.
            m_argument = byteCodes[bytePointer++];
            break;

        case opcode::doSpecial:
            switch (m_argument) {
                case special::branch:
                case special::branchIfTrue:
                case special::branchIfFalse:
                    // Storing jump target offset as extra
                    m_extra = byteCodes[bytePointer] | (byteCodes[bytePointer+1] << 8);
                    bytePointer += 2;
                    break;

                case special::sendToSuper:
                    m_extra = byteCodes[bytePointer++];
                    break;
            }
            break;

        default: // Nothing to do here
            break;
    }
}
