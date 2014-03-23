#include <instructions.h>

using namespace st;

void ParsedMethod::parseBlock(uint16_t startOffset, uint16_t stopOffset) {
    // Following instruction belong to the nested code block
    // ParsedBlock will decode all of it's instructions and nested blocks
    ParsedBlock* parsedBlock = new ParsedBlock(this, startOffset, stopOffset);
    m_offsetToParsedBlock[startOffset] = parsedBlock;
}

ParsedMethod::~ParsedMethod() {
    for (TParsedBlockList::iterator iBlock = m_parsedBlocks.begin(),
        end = m_parsedBlocks.end(); iBlock != end; ++iBlock)
    {
        delete * iBlock;
    }
}
