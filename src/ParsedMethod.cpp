#include <instructions.h>

using namespace st;

void ParsedMethod::parse(TMethod* method) {
    assert(method);
    assert(method->byteCodes);

    uint16_t bytePointer = 0;

    TByteObject&   byteCodes   = * method->byteCodes;
    const uint16_t stopPointer = byteCodes.getSize();

    // Scaning the method's bytecodes for branch sites and collecting branch targets.
    // Creating target basic blocks beforehand and collecting them in a map.
    while (bytePointer < stopPointer) {
        // Decoding instruction and shifting byte pointer to the next one
        TSmalltalkInstruction instruction(byteCodes, bytePointer);

        if (instruction.getOpcode() == opcode::pushBlock) {
            // Skipping the nested block's bytecodes
            // Extra holds the new bytecode offset
            bytePointer = instruction.getExtra();
            continue;
        }

        // We're now looking only for branch bytecodes
        if (instruction.getOpcode() != opcode::doSpecial)
            continue;

        switch (instruction.getArgument()) {
            case special::branch:
            case special::branchIfTrue:
            case special::branchIfFalse: {
                const uint16_t targetOffset = instruction.getExtra();

                // Checking whether current branch target is already known
                if (m_offsetToBasicBlock.find(targetOffset) == m_offsetToBasicBlock.end()) {
                    // Creating the referred basic block and inserting it into the function
                    // Later it will be filled with instructions and linked to other blocks
                    BasicBlock* targetBasicBlock = createBasicBlock(bytePointer);
                    m_offsetToBasicBlock[targetOffset] = targetBasicBlock;
                }
            } break;
        }
    }

    // Populating previously created basic blocks with actual instructions
    BasicBlock* currentBasicBlock = m_offsetToBasicBlock[0];

    // If no branch site points to 0 offset then we create block ourselves
    if (! currentBasicBlock) {
        m_offsetToBasicBlock[0] = currentBasicBlock = new BasicBlock(0);

        // Pushing block from the beginning to comply it's position
        m_basicBlocks.push_front(currentBasicBlock);
    }

    while (bytePointer < stopPointer) {
        // Switching basic block if current offset is a branch target
        const TOffsetToBasicBlockMap::iterator iBlock = m_offsetToBasicBlock.find(bytePointer);
        if (iBlock != m_offsetToBasicBlock.end())
            currentBasicBlock = iBlock->second;

        // Fetching instruction and appending it to the current basic block
        TSmalltalkInstruction instruction(byteCodes, bytePointer);
        currentBasicBlock->append(instruction);
    }
}
