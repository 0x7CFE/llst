#include <cstdio>
#include <instructions.h>

using namespace st;

void ParsedBytecode::parse(uint16_t startOffset, uint16_t stopOffset) {
    assert(m_origin && m_origin->byteCodes);

    InstructionDecoder decoder(*m_origin->byteCodes, startOffset);

    TByteObject&   byteCodes   = * m_origin->byteCodes;
    const uint16_t stopPointer = stopOffset ? stopOffset : byteCodes.getSize();

    std::printf("Phase 1. Collecting branch instructions and building blocks\n");

    // Scaning the method's bytecodes for branch sites and collecting branch targets.
    // Creating target basic blocks beforehand and collecting them in a map.
    while (decoder.getBytePointer() < stopPointer) {
        const uint16_t currentBytePointer = decoder.getBytePointer();
        TSmalltalkInstruction instruction = decoder.decodeAndShiftPointer();

        if (instruction.getOpcode() == opcode::pushBlock) {
            // Preserving the start block's offset
            const uint16_t blockStartOffset = decoder.getBytePointer();
            // Extra holds the bytecode offset right after the block
            const uint16_t blockStopOffset  = instruction.getExtra();

            // Parsing block. This operation depends on
            // whether we're in a method or in a block.
            // Nested blocks are registered in the
            // container method, not the outer block.
            std::printf("%.4u : Parsing smalltalk block in interval [%u:%u)\n", currentBytePointer, blockStartOffset, blockStopOffset);
            parseBlock(blockStartOffset, blockStopOffset);

            // Skipping the nested block's bytecodes
            decoder.setBytePointer(blockStopOffset);
            continue;
        }

        // We're now looking only for branch bytecodes
        if (instruction.getOpcode() != opcode::doSpecial)
            continue;

        switch (instruction.getArgument()) {
            case special::branchIfTrue:
            case special::branchIfFalse: {
                // Processing skip block
                const uint16_t skipOffset  = decoder.getBytePointer();
                BasicBlock* skipBasicBlock = createBasicBlock(skipOffset);
                std::printf("%.4u : branch to skip block %p (%u)\n", currentBytePointer, skipBasicBlock, skipOffset);
            } // no break here

            case special::branch: {
                // Processing target block
                const uint16_t targetOffset  = instruction.getExtra();
                BasicBlock* targetBasicBlock = createBasicBlock(targetOffset);
                std::printf("%.4u : branch to target block %p (%u)\n", currentBytePointer, targetBasicBlock, targetOffset);
            } break;
        }
    }

    std::printf("Phase 2. Populating blocks with instructions\n");

    // Populating previously created basic blocks with actual instructions
    BasicBlock* currentBasicBlock = m_offsetToBasicBlock[startOffset];


    // If no branch site points to start offset then we create block ourselves
    if (! currentBasicBlock) {
        m_offsetToBasicBlock[startOffset] = currentBasicBlock = new BasicBlock(startOffset);

        // Pushing block from the beginning to comply it's position
        m_basicBlocks.push_front(currentBasicBlock);
    }

    std::printf("Initial block is %p offset %u\n", currentBasicBlock, currentBasicBlock->getOffset());

    // Instructions in a basic block that follow a terminator instruction
    // will never be executed because control flow will never reach them.
    // We need to skip such instructions because they may distort the actual
    // control flow by introducing fake block dependencies.
    bool terminatorEncoded = false;

    decoder.setBytePointer(startOffset);
    while (decoder.getBytePointer() < stopPointer) {
        const uint16_t currentBytePointer = decoder.getBytePointer();
        if (currentBytePointer != startOffset) {
            // Switching basic block if current offset is a branch target
            const TOffsetToBasicBlockMap::iterator iBlock = m_offsetToBasicBlock.find(currentBytePointer);
            if (iBlock != m_offsetToBasicBlock.end()) {
                BasicBlock* const nextBlock = iBlock->second;

                // Checking if previous block referred current block
                // by jumping into it and adding reference if needed
                updateReferences(currentBasicBlock, nextBlock, decoder);

                // Switching to a new block
                currentBasicBlock = nextBlock;

                // Resetting the terminator flag
                terminatorEncoded = false;

                std::printf("%.4u : now working on block %p offset %u\n", currentBytePointer, currentBasicBlock, currentBasicBlock->getOffset());
            }
        }

        // Fetching instruction and appending it to the current basic block
        TSmalltalkInstruction instruction = decoder.decodeAndShiftPointer();

        // Skipping dead code
        if (terminatorEncoded) {
            std::printf("%.4u : skipping dead code\n", currentBytePointer);
            continue;
        } else {
            currentBasicBlock->append(instruction);
        }

        // Skipping nested smalltalk block bytecodes
        if (instruction.getOpcode() == opcode::pushBlock) {
            decoder.setBytePointer(instruction.getExtra());
            continue;
        }

        if (instruction.isTerminator()) {
            std::printf("%.4u : terminator encoded\n", currentBytePointer);
            terminatorEncoded = true;
        }

        // Final check for the last instruction of the method
        if (decoder.getBytePointer() >= stopPointer && instruction.isBranch()) {
            const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(instruction.getExtra());
            if (iTargetBlock != m_offsetToBasicBlock.end()) {
                iTargetBlock->second->getReferers().insert(currentBasicBlock);
                std::printf("%.4u : block reference %p (%u) ->? %p (%u)\n", currentBytePointer, currentBasicBlock, currentBasicBlock->getOffset(), iTargetBlock->second, iTargetBlock->first);
            } else
                assert(false);
        }
    }
}

BasicBlock* ParsedBytecode::createBasicBlock(uint16_t blockOffset) {
    // Checking whether current branch target is already known
    TOffsetToBasicBlockMap::iterator iBlock = m_offsetToBasicBlock.find(blockOffset);
    if (iBlock != m_offsetToBasicBlock.end())
        return iBlock->second;

    // Creating the referred basic block and inserting it into the function
    // Later it will be filled with instructions and linked to other blocks
    BasicBlock* const newBasicBlock = new BasicBlock(blockOffset);
    m_offsetToBasicBlock[blockOffset] = newBasicBlock;
    m_basicBlocks.push_back(newBasicBlock);

    return newBasicBlock;
}

void ParsedBytecode::updateReferences(BasicBlock* currentBasicBlock, BasicBlock* nextBlock, InstructionDecoder& decoder) {
    TSmalltalkInstruction terminator(opcode::extended);
    if (currentBasicBlock->getTerminator(terminator)) {
        if (terminator.isBranch()) {
            if (terminator.getArgument() == special::branch) {
                // Unconditional branch case
                const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(terminator.getExtra());
                if (iTargetBlock != m_offsetToBasicBlock.end()) {
                    std::printf("%.4u : block reference %p (%u) -> %p (%u)\n", decoder.getBytePointer(), currentBasicBlock, currentBasicBlock->getOffset(), iTargetBlock->second, iTargetBlock->first);
                    iTargetBlock->second->getReferers().insert(currentBasicBlock);
                } else {
                    assert(false);
                }
            } else {
                // Previous block referred some other block instead.
                // Terminator is one of conditional branch instructions.
                // We need to refer both of branch targets here.
                assert(terminator.getArgument() == special::branchIfTrue
                || terminator.getArgument() == special::branchIfFalse);

                // Case when branch condition is not met
                std::printf("%.4u : block reference %p (%u) ->F %p (%u)\n", decoder.getBytePointer(), currentBasicBlock, currentBasicBlock->getOffset(), nextBlock, nextBlock->getOffset());
                nextBlock->getReferers().insert(currentBasicBlock);

                // Case when branch condition is met
                const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(terminator.getExtra());
                if (iTargetBlock != m_offsetToBasicBlock.end()) {
                    std::printf("%.4u : block reference %p (%u) ->T %p (%u)\n", decoder.getBytePointer(), currentBasicBlock, currentBasicBlock->getOffset(), iTargetBlock->second, iTargetBlock->first);
                    iTargetBlock->second->getReferers().insert(currentBasicBlock);
                } else {
                    assert(false);
                }
            }
        }
    } else {
        // Adding branch instruction to link blocks
        currentBasicBlock->append(TSmalltalkInstruction(opcode::doSpecial, special::branch, decoder.getBytePointer()));
        nextBlock->getReferers().insert(currentBasicBlock);

        std::printf("%.4u : linking blocks %p (%u) ->  %p (%u) with branch instruction\n", decoder.getBytePointer(), currentBasicBlock, currentBasicBlock->getOffset(), nextBlock, nextBlock->getOffset());
    }
}