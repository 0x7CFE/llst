#include <instructions.h>

using namespace st;

void ParsedBlock::parseBlock(uint16_t startOffset, uint16_t stopOffset) {
    ParsedMethod* const container   = getContainer();
    ParsedBlock*  const nestedBlock = new ParsedBlock(container, startOffset, stopOffset);

    container->addParsedBlock(nestedBlock);
}
