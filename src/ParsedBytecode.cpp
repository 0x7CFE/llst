#include <cstdio>
#include <stapi.h>

using namespace st;

static const bool traces_enabled = false;

void ParsedBytecode::parse(uint16_t startOffset, uint16_t stopOffset) {
    assert(m_origin && m_origin->byteCodes);

    InstructionDecoder decoder(*m_origin->byteCodes, startOffset);

    TByteObject&   byteCodes   = * m_origin->byteCodes;
    const uint16_t stopPointer = stopOffset ? stopOffset : byteCodes.getSize();

    if (traces_enabled)
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

            if (traces_enabled)
                std::printf("%.4u : Parsing smalltalk block in interval [%u:%u)\n", currentBytePointer, blockStartOffset, blockStopOffset);

            // Parsing block. This operation depends on
            // whether we're in a method or in a block.
            // Nested blocks are registered in the
            // container method, not the outer block.
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

                if (traces_enabled)
                    std::printf("%.4u : branch to skip block %p (%u)\n", currentBytePointer, skipBasicBlock, skipOffset);
            } // no break here

            case special::branch: {
                // Processing target block
                const uint16_t targetOffset  = instruction.getExtra();
                BasicBlock* targetBasicBlock = createBasicBlock(targetOffset);

                if (traces_enabled)
                    std::printf("%.4u : branch to target block %p (%u)\n", currentBytePointer, targetBasicBlock, targetOffset);
            } break;
        }
    }

    if (traces_enabled)
        std::printf("Phase 2. Populating blocks with instructions\n");

    // Populating previously created basic blocks with actual instructions
    BasicBlock* currentBasicBlock = m_offsetToBasicBlock[startOffset];


    // If no branch site points to start offset then we create block ourselves
    if (! currentBasicBlock) {
        m_offsetToBasicBlock[startOffset] = currentBasicBlock = new BasicBlock(startOffset);

        if (traces_enabled)
            std::printf("created start basic block %p (%u)\n", currentBasicBlock, startOffset);

        // Pushing block from the beginning to comply it's position
        m_basicBlocks.push_front(currentBasicBlock);
    }

    if (traces_enabled)
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

                if (traces_enabled)
                    std::printf("%.4u : now working on block %p offset %u\n", currentBytePointer, currentBasicBlock, currentBasicBlock->getOffset());
            }
        }

        // Fetching instruction and appending it to the current basic block
        TSmalltalkInstruction instruction = decoder.decodeAndShiftPointer();

        // Skipping dead code
        if (terminatorEncoded) { // TODO In case of dead branches erase the target blocks
            if (traces_enabled)
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
            if (traces_enabled)
                std::printf("%.4u : terminator encoded\n", currentBytePointer);

            terminatorEncoded = true;
        }

        // Final check for the last instruction of the method
        if (decoder.getBytePointer() >= stopPointer && instruction.isBranch()) {
            const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(instruction.getExtra());
            assert(iTargetBlock != m_offsetToBasicBlock.end());

            iTargetBlock->second->getReferers().insert(currentBasicBlock);
            if (traces_enabled)
                std::printf("%.4u : block reference %p (%u) ->? %p (%u)\n", currentBytePointer, currentBasicBlock, currentBasicBlock->getOffset(), iTargetBlock->second, iTargetBlock->first);
        }
    }

    if (traces_enabled)
        std::printf("Phase 3. Wiping out chains of unreachable blocks\n");

    // At this stage all possible branches are encoded and block relations are registered in the referer sets.
    // We may now iterate through the blocks and wipe out unreachable ones. We need to iterate several times
    // because unreachable blocks may form a chain. Stil we may not guarantee, that all unreachable blocks will
    // be removed because they may form a cycle where each block refers the other and all have at least 1 referer.
    while (true) {
        bool blockRemoved = false;

        // Iterating through basic blocks to find blocks that have zero referers and do not start at startOffset
        TBasicBlockList::iterator iBlock = m_basicBlocks.begin();
        while (iBlock != m_basicBlocks.end()) {
            BasicBlock* const block = *iBlock;

            if (block->getReferers().empty() && block->getOffset() != startOffset) {
                if (traces_enabled)
                    std::printf("block %p (%u) is not reachable, erasing and clearing references\n", block, block->getOffset());

                TSmalltalkInstruction terminator(opcode::extended);
                if (block->getTerminator(terminator) && terminator.isBranch()) {
                    const uint16_t targetOffset = terminator.getExtra();
                    const uint16_t skipOffset   = (terminator.getArgument() == special::branch) ? 0 : getNextBlockOffset(block, stopOffset);

                    eraseReferer(targetOffset, block);
                    if (skipOffset)
                        eraseReferer(skipOffset, block);
                }

                TBasicBlockList::iterator currentBlock = iBlock++;
                eraseBasicBlock(currentBlock);
                blockRemoved = true;
                continue;
            }

            ++iBlock;
        }

        // If no block was removed we need to stop
        if (!blockRemoved)
            break;
    }
}

void ParsedBytecode::eraseBasicBlock(ParsedBytecode::iterator& iBlock)
{
    BasicBlock* const block = *iBlock;
    m_offsetToBasicBlock.erase(block->getOffset());
    m_basicBlocks.erase(iBlock);
    delete block;
}

void ParsedBytecode::eraseReferer(uint16_t targetOffset, BasicBlock* referer) {
    const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(targetOffset);
    assert(iTargetBlock != m_offsetToBasicBlock.end());

    BasicBlock* const target = iTargetBlock->second;

    if (traces_enabled)
        std::printf("erasing reference %p (%u) -> %p (%u)\n", referer, referer->getOffset(), target, target->getOffset());

    target->getReferers().erase(referer);
}

uint16_t ParsedBytecode::getNextBlockOffset(BasicBlock* currentBlock, uint16_t stopOffset) {
    for (uint16_t offset = currentBlock->getOffset() + 1; offset < stopOffset; offset++)
        if (m_offsetToBasicBlock.find(offset) != m_offsetToBasicBlock.end())
            return offset;

    return 0;
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

    if (traces_enabled)
        std::printf("created new basic block %p (%u)\n", newBasicBlock, newBasicBlock->getOffset());

    return newBasicBlock;
}

void ParsedBytecode::updateReferences(BasicBlock* currentBasicBlock, BasicBlock* nextBlock, InstructionDecoder& decoder) {
    TSmalltalkInstruction terminator(opcode::extended);
    if (currentBasicBlock->getTerminator(terminator)) {
        if (terminator.isBranch()) {
            if (terminator.getArgument() == special::branch) {
                // Unconditional branch case
                const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(terminator.getExtra());
                assert(iTargetBlock != m_offsetToBasicBlock.end());

                if (traces_enabled)
                    std::printf("%.4u : block reference %p (%u) -> %p (%u)\n", decoder.getBytePointer(), currentBasicBlock, currentBasicBlock->getOffset(), iTargetBlock->second, iTargetBlock->first);

                iTargetBlock->second->getReferers().insert(currentBasicBlock);
            } else {
                // Previous block referred some other block instead.
                // Terminator is one of conditional branch instructions.
                // We need to refer both of branch targets here.
                assert(terminator.getArgument() == special::branchIfTrue
                    || terminator.getArgument() == special::branchIfFalse);

                if (traces_enabled)
                    std::printf("%.4u : block reference %p (%u) ->F %p (%u)\n", decoder.getBytePointer(), currentBasicBlock, currentBasicBlock->getOffset(), nextBlock, nextBlock->getOffset());

                // Case when branch condition is not met
                nextBlock->getReferers().insert(currentBasicBlock);

                // Case when branch condition is met
                const TOffsetToBasicBlockMap::iterator iTargetBlock = m_offsetToBasicBlock.find(terminator.getExtra());
                assert(iTargetBlock != m_offsetToBasicBlock.end());

                if (traces_enabled)
                    std::printf("%.4u : block reference %p (%u) ->T %p (%u)\n", decoder.getBytePointer(), currentBasicBlock, currentBasicBlock->getOffset(), iTargetBlock->second, iTargetBlock->first);

                iTargetBlock->second->getReferers().insert(currentBasicBlock);
            }
        }
    } else {
        // Adding branch instruction to link blocks
        currentBasicBlock->append(TSmalltalkInstruction(opcode::doSpecial, special::branch, decoder.getBytePointer()));
        nextBlock->getReferers().insert(currentBasicBlock);

        if (traces_enabled)
            std::printf("%.4u : linking blocks %p (%u) ->  %p (%u) with branch instruction\n", decoder.getBytePointer(), currentBasicBlock, currentBasicBlock->getOffset(), nextBlock, nextBlock->getOffset());
    }
}
