#include <instructions.h>

using namespace st;

const TSmalltalkInstruction InstructionDecoder::decodeAndShiftPointer(const TByteObject& byteCodes, uint16_t& bytePointer)
{
    TSmalltalkInstruction::TOpcode   opcode;
    TSmalltalkInstruction::TArgument argument;
    TSmalltalkInstruction::TExtra    extra = 0;

    assert(bytePointer < byteCodes.getSize());
    const uint8_t& bytecode = byteCodes[bytePointer++];

    // For normal bytecodes higher part of the byte holds opcode
    // whether lower part holds the argument
    opcode   = static_cast<TSmalltalkInstruction::TOpcode>(bytecode >> 4);
    argument = bytecode & 0x0F;

    // Extended opcodes encode argument in a separate byte
    // Opcode is stored in a lower half of the first byte
    if (opcode == opcode::extended) {
        opcode   = static_cast<TSmalltalkInstruction::TOpcode>(argument);
        argument = byteCodes[bytePointer++];
    }

    // Some instructions hold extra data in a bytes right after instruction
    switch (opcode) {
        case opcode::pushBlock:
            // Storing bytecode offset as extra
            extra = byteCodes[bytePointer] | (byteCodes[bytePointer+1] << 8);
            bytePointer += 2;
            break;

        case opcode::doPrimitive:
            // The index number of the primitive number does not fit 4 lower bits of the opcode.
            // So it is stored in a separate byte right after 'argument'.
            extra = byteCodes[bytePointer++];
            break;

        case opcode::doSpecial:
            switch (argument) {
                case special::branch:
                case special::branchIfTrue:
                case special::branchIfFalse:
                    // Storing jump target offset as extra
                    extra = byteCodes[bytePointer] | (byteCodes[bytePointer+1] << 8);
                    bytePointer += 2;
                    break;

                case special::sendToSuper:
                    extra = byteCodes[bytePointer++];
                    break;
            }
            break;

        default: // Nothing to do here
            break;
    }

    return TSmalltalkInstruction(opcode, argument, extra);
}
