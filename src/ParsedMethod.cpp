#include <instructions.h>

using namespace st;

void ParsedMethod::parseBlock(uint16_t startOffset, uint16_t stopOffset) {
    // Following instruction belong to the nested code block
    // ParsedBlock will decode all of it's instructions and nested blocks
    ParsedBlock* parsedBlock = new ParsedBlock(this, startOffset, stopOffset);
    m_offsetToParsedBlock[startOffset] = parsedBlock;
}

void ParsedMethod::parse(uint16_t startOffset, uint16_t stopOffset) {
    assert(m_origin && m_origin->byteCodes);

    uint16_t bytePointer = startOffset;

    TByteObject&   byteCodes   = * m_origin->byteCodes;
    const uint16_t stopPointer = stopOffset ? stopOffset : byteCodes.getSize();

    // Scaning the method's bytecodes for branch sites and collecting branch targets.
    // Creating target basic blocks beforehand and collecting them in a map.
    while (bytePointer < stopPointer) {
        // Decoding instruction and shifting byte pointer to the next one
        TSmalltalkInstruction instruction(byteCodes, bytePointer);

        if (instruction.getOpcode() == opcode::pushBlock) {
            // Preserving the start block's offset
            const uint16_t startOffset = bytePointer;
            // Extra holds the bytecode offset right after the block
            const uint16_t stopOffset  = instruction.getExtra();

            // Parsing block. This operation depends on
            // whether we're in a method or in a block.
            // Nested blocks are registered in the
            // container method, not the outer block.
            parseBlock(startOffset, stopOffset);

            // Skipping the nested block's bytecodes
            bytePointer = stopOffset;
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
    BasicBlock* currentBasicBlock = m_offsetToBasicBlock[startOffset];

    // If no branch site points to start offset then we create block ourselves
    if (! currentBasicBlock) {
        m_offsetToBasicBlock[startOffset] = currentBasicBlock = new BasicBlock(startOffset);

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
