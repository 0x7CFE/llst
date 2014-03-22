#ifndef LLST_ANALYSIS_INCLUDED
#define LLST_ANALYSIS_INCLUDED

#include <instructions.h>

namespace st {

// This detector scans method's nested blocks for block return instruction.
// Only block's instructions are traversed. Method's instructions are skipped.
//
// This pass is used to find out whether method code contains block return instruction.
// This instruction is handled in a very different way than the usual opcodes.
// Thus requires special handling. Block return is done by trowing an exception out of
// the block containing it. Then it's catched by the method's code to perform a return.
// In order not to bloat the code with unused try-catch code we're previously scanning
// the method's code to ensure that try-catch is really needed. If it is not, we simply
// skip its generation.
class BlockReturnDetector : public ParsedBlockVisitor {
public:
    BlockReturnDetector(ParsedMethod* parsedMethod) : ParsedBlockVisitor(parsedMethod), m_blockReturnFound(false) { }

    bool isBlockReturnFound() const { return m_blockReturnFound; }

protected:
    class InstructionDetector : public InstructionVisitor {
    public:
        InstructionDetector(ParsedBytecode* parsedBytecode) : InstructionVisitor(parsedBytecode), m_blockReturnFound(false) { }
        bool isBlockReturnFound() const { return m_blockReturnFound; }

    private:
        virtual bool visitInstruction(const TSmalltalkInstruction& instruction) {
            if (instruction.getOpcode() == opcode::doSpecial) {
               if (instruction.getArgument() == special::blockReturn) {
                   m_blockReturnFound = true;
                   return false;
               }
            }

            return true;
        }

        bool m_blockReturnFound;
    };

    virtual bool visitBlock(ParsedBlock& parsedBlock) {
        InstructionDetector detector(&parsedBlock);
        detector.run();

        if (detector.isBlockReturnFound()) {
            m_blockReturnFound = true;
            return false;
        }

        return true;
    }

private:
    bool m_blockReturnFound;
};

} // namespace st

#endif
