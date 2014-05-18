#include <instructions.h>

using namespace st;

void ParsedBlock::parseBlock(uint16_t startOffset, uint16_t stopOffset) {
    ParsedMethod* container = getContainer();

    ParsedBlock* nestedBlock = new ParsedBlock(container, startOffset, stopOffset);
    container->addParsedBlock(nestedBlock);
}
