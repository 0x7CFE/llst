#include <instructions.h>

using namespace st;

void ParsedBytecode::parse(uint16_t startOffset, uint16_t stopOffset) {
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
        if (iBlock != m_offsetToBasicBlock.end()) {
            BasicBlock* const nextBlock = iBlock->second;

            // Checking if previous block referred current block
            // by jumping into it and adding reference if needed
            TSmalltalkInstruction terminator(opcode::extended);
            if (currentBasicBlock->getTerminator(terminator)) {
                if (terminator.isBranch()) {
                    if (terminator.getExtra() == bytePointer) {
                        // Unconditional branch case
                        assert(terminator.getArgument() == special::branch);
                        nextBlock->getReferers().insert(currentBasicBlock);
                    } else {
                        // Previous block referred some other block instead.
                        // Terminator is one of conditional branch instructions.
                        // We need to refer both of branch targets here.
                        assert(terminator.getArgument() == special::branchIfTrue
                            || terminator.getArgument() == special::branchIfFalse);

                        // Case when branch condition is not met
                        nextBlock->getReferers().insert(currentBasicBlock);

                        // Case when branch condition is met
                        const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(terminator.getExtra());
                        if (iTargetBlock != m_offsetToBasicBlock.end())
                            iTargetBlock->second->getReferers().insert(currentBasicBlock);
                        else
                            assert(false);
                    }
                }
            } else {
                // Adding branch instruction to link blocks
                currentBasicBlock->append(TSmalltalkInstruction(opcode::doSpecial, special::branch, bytePointer));
                nextBlock->getReferers().insert(currentBasicBlock);
            }

            // Switching to a new block
            currentBasicBlock = nextBlock;
        }

        // Fetching instruction and appending it to the current basic block
        TSmalltalkInstruction instruction(byteCodes, bytePointer);
        currentBasicBlock->append(instruction);

        // Final check
        if (instruction.isBranch()) {
            const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(instruction.getExtra());
            if (iTargetBlock != m_offsetToBasicBlock.end())
                iTargetBlock->second->getReferers().insert(currentBasicBlock);
            else
                assert(false);
        }
    }
}
