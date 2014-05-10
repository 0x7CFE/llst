#ifndef LLST_INSTRUCTIONS_INCLUDED
#define LLST_INSTRUCTIONS_INCLUDED

#include <assert.h>
#include <stdint.h>
#include <vector>
#include <list>
#include <map>
#include <set>

#include <types.h>
#include <opcodes.h>

namespace st {

struct TSmalltalkInstruction {
    typedef opcode::Opcode TOpcode;
    typedef uint8_t        TArgument;
    typedef uint16_t       TExtra;
    typedef uint32_t       TUnpackedBytecode;

    TSmalltalkInstruction(TOpcode opcode, TArgument argument = 0, TExtra extra = 0)
        : m_opcode(opcode), m_argument(argument), m_extra(extra) {}

    // Initialize instruction from the unpacked value
    TSmalltalkInstruction(TUnpackedBytecode bytecode) {
        m_opcode   = static_cast<TOpcode>(bytecode & 0xFF);
        m_argument = static_cast<TArgument>(bytecode >> 8);
        m_extra    = static_cast<TExtra>(bytecode >> 16);
    }

    TOpcode getOpcode() const { return m_opcode; }
    TArgument getArgument() const { return m_argument; }
    TExtra getExtra() const { return m_extra; }

    // Return fixed width representation of bytecode suitable for storing in arrays
    TUnpackedBytecode serialize() const {
        return
            static_cast<TUnpackedBytecode>(m_opcode) |
            static_cast<TUnpackedBytecode>(m_argument) << 8 |
            static_cast<TUnpackedBytecode>(m_extra) << 16;
    }

    bool isTerminator() const {
        if (m_opcode != opcode::doSpecial)
            return false;

        if (isBranch())
            return true;

        switch (m_argument) {
            case special::stackReturn:
            case special::selfReturn:
            case special::blockReturn:
                return true;

            default:
                return false;
        }
    }

    bool isBranch() const {
        if (m_opcode != opcode::doSpecial)
            return false;

        switch (m_argument) {
            case special::branch:
            case special::branchIfFalse:
            case special::branchIfTrue:
                return true;

            default:
                return false;
        }
    }

private:
    TOpcode   m_opcode;
    TArgument m_argument;
    TExtra    m_extra;
};

class InstructionDecoder {
public:
    InstructionDecoder(const TByteObject& byteCodes, uint16_t bytePointer = 0)
        : m_byteCodes(byteCodes), m_bytePointer(bytePointer) {}

    uint16_t getBytePointer() const { return m_bytePointer; }
    void setBytePointer(uint16_t value) {
        assert(value < m_byteCodes.getSize());
        m_bytePointer = value;
    }

    const TSmalltalkInstruction decodeAndShiftPointer() {
        return decodeAndShiftPointer(m_byteCodes, m_bytePointer);
    }

    static const TSmalltalkInstruction decodeAndShiftPointer(const TByteObject& byteCodes, uint16_t& bytePointer);
private:
    const TByteObject& m_byteCodes;
    uint16_t m_bytePointer;
};

class BasicBlock {
public:
    typedef std::vector<TSmalltalkInstruction::TUnpackedBytecode> TInstructionVector;
    typedef std::set<BasicBlock*> TBasicBlockSet;

    class iterator : public TInstructionVector::iterator {
    public:
        iterator(const TInstructionVector::iterator& copy) : TInstructionVector::iterator(copy) { }

        const TSmalltalkInstruction operator *() const {
            return TSmalltalkInstruction(TInstructionVector::iterator::operator*());
        }

        TInstructionVector::iterator& get() { return static_cast<TInstructionVector::iterator&>(*this); }
        const TInstructionVector::iterator& get() const { return static_cast<const TInstructionVector::iterator&>(*this); }
    };

    iterator begin() { return iterator(m_instructions.begin()); }
    iterator end() { return iterator(m_instructions.end()); }

    std::size_t size() const { return m_instructions.size(); }

    const TSmalltalkInstruction operator[](const std::size_t index) const {
        return TSmalltalkInstruction(m_instructions[index]);
    }

    // Append instruction to the end of basic block
    void append(TSmalltalkInstruction instruction) {
        m_instructions.push_back(instruction.serialize());
    }

    // Insert instruction at specified position
    void insert(const iterator& position, TSmalltalkInstruction instruction) {
        m_instructions.insert(position, instruction.serialize());
    }

    // Replace existing instruction at specified position with the new one
    void replace(const iterator& position, TSmalltalkInstruction instruction) {
        assert(position != m_instructions.end());

        const TInstructionVector::iterator replacePosition = position;
        *replacePosition = instruction.serialize();
    }

    // Remove instruction from basic block
    void remove(const iterator& position) {
        assert(position != m_instructions.end());
        m_instructions.erase(position);
    }

    // Split current basic block at specified position
    // Current block will hold instructions prior to the cut position
    // Returned block will hold the rest
    BasicBlock* split(const iterator& position) {
        BasicBlock* newBlock = new BasicBlock;
        std::copy(position.get(), m_instructions.end(), newBlock->m_instructions.begin());
        m_instructions.erase(position, m_instructions.end());
        // TODO insert jump instruction and add newBlock to the parsed method
        return newBlock;
    }

    // Offset of first instruction of this basic block within the method's bytecodes
    uint16_t getOffset() const { return m_offset; }

    // Set of basic blocks that are referencing current block by [conditionally] jumping into it
    TBasicBlockSet& getReferers() { return m_referers; }

    bool getTerminator(TSmalltalkInstruction& out) const {
        if (m_instructions.empty())
            return false;

        TSmalltalkInstruction result(m_instructions.back());
        if (result.isTerminator()) {
            out = result;
            return true;
        }

        return false;
    }

    BasicBlock(uint16_t blockOffset = 0) : m_offset(blockOffset) { }
private:
    uint16_t m_offset;
    TInstructionVector m_instructions;
    TBasicBlockSet     m_referers;
};

// This is a base class for ParsedMethod and ParsedBlock
// Provides common interface for iterating basic blocks and
// instructions which is used by corresponding visitors
class ParsedBytecode {
public:
    typedef std::list<BasicBlock*> TBasicBlockList;
    typedef TBasicBlockList::iterator iterator;
    iterator begin() { return m_basicBlocks.begin(); }
    iterator end() { return m_basicBlocks.end(); }

    BasicBlock* createBasicBlock(uint16_t blockOffset);

    virtual ~ParsedBytecode() {
        for (TBasicBlockList::iterator iBlock = m_basicBlocks.begin(),
            end = m_basicBlocks.end(); iBlock != end; ++iBlock)
        {
            delete * iBlock;
        }
    }

    // Returns actual method object which was parsed
    TMethod* getOrigin() const { return m_origin; }

    BasicBlock* getBasicBlockByOffset(uint16_t offset) {
        TOffsetToBasicBlockMap::iterator iBlock = m_offsetToBasicBlock.find(offset);
        if (iBlock != m_offsetToBasicBlock.end())
            return iBlock->second;
        else
            return 0;
    }

protected:
    ParsedBytecode(TMethod* method) : m_origin(method) { }
    void parse(uint16_t startOffset = 0, uint16_t stopOffset = 0);

    // Descendants should override this method to provide block handling
    virtual void parseBlock(uint16_t startOffset, uint16_t stopOffset) = 0;

protected:
    TMethod* m_origin;
    TBasicBlockList m_basicBlocks;

    typedef std::map<uint16_t, BasicBlock*> TOffsetToBasicBlockMap;
    TOffsetToBasicBlockMap m_offsetToBasicBlock;

private:
    void updateReferences(BasicBlock* currentBasicBlock, BasicBlock* nextBlock, InstructionDecoder& decoder);
};

class ParsedBlock;
class ParsedMethod : public ParsedBytecode {
    friend class ParsedBlock; // for addParsedBlock()

public:
    typedef std::list<ParsedBlock*> TParsedBlockList;

    ParsedMethod(TMethod* method) : ParsedBytecode(method) {
        assert(method);
        parse();
    }

    virtual ~ParsedMethod();

    typedef TParsedBlockList::iterator block_iterator;
    block_iterator blockBegin() { return m_parsedBlocks.begin(); }
    block_iterator blockEnd() { return m_parsedBlocks.end(); }

    ParsedBlock* getParsedBlockByOffset(uint16_t startOffset) {
        TOffsetToParsedBlockMap::iterator iBlock = m_offsetToParsedBlock.find(startOffset);
        if (iBlock != m_offsetToParsedBlock.end())
            return iBlock->second;
        else
            return 0;
    }

protected:
    virtual void parseBlock(uint16_t startOffset, uint16_t stopOffset);

    void addParsedBlock(uint16_t offset, ParsedBlock* parsedBlock) {
        m_parsedBlocks.push_back(parsedBlock);
        m_offsetToParsedBlock[offset] = parsedBlock;
    }

protected:
    TParsedBlockList m_parsedBlocks;

    typedef std::map<uint16_t, ParsedBlock*> TOffsetToParsedBlockMap;
    TOffsetToParsedBlockMap m_offsetToParsedBlock;
};

class ParsedBlock : public ParsedBytecode {
public:
    ParsedBlock(ParsedMethod* parsedMethod, uint16_t startOffset, uint16_t stopOffset)
        : ParsedBytecode(parsedMethod->getOrigin()), m_containerMethod(parsedMethod),
          m_startOffset(startOffset), m_stopOffset(stopOffset)
    {
        parse(startOffset, stopOffset);
    }

    // Parsed method encapsulating the block's bytecodes
    ParsedMethod* getContainer() const { return m_containerMethod; }

    // First instruction offset within method's bytecodes
    uint16_t getStartOffset() const { return m_startOffset; }

    // Instruction offset after the last block's instruction
    uint16_t getStopOffset() const { return m_stopOffset; }

protected:
    virtual void parseBlock(uint16_t startOffset, uint16_t stopOffset);

protected:
    ParsedMethod* m_containerMethod;
    uint16_t m_startOffset;
    uint16_t m_stopOffset;
};

class BasicBlockVisitor {
public:
    BasicBlockVisitor(ParsedBytecode* parsedBytecode) : m_parsedBytecode(parsedBytecode) { }
    virtual ~BasicBlockVisitor() { }

    virtual bool visitBlock(BasicBlock& basicBlock) { return true; }

    void run() {
        ParsedBytecode::iterator iBlock = m_parsedBytecode->begin();
        const ParsedBytecode::iterator iEnd = m_parsedBytecode->end();

        while (iBlock != iEnd) {
            if (! visitBlock(** iBlock))
                break;

            ++iBlock;
        }
    }

protected:
    ParsedBytecode* m_parsedBytecode;
};

class InstructionVisitor : public BasicBlockVisitor {
public:
    InstructionVisitor(ParsedBytecode* parsedBytecode) : BasicBlockVisitor(parsedBytecode) { }
    virtual bool visitInstruction(const TSmalltalkInstruction& instruction) { return true; }

protected:
    virtual bool visitBlock(BasicBlock& basicBlock) {
        BasicBlock::iterator iInstruction = basicBlock.begin();
        const BasicBlock::iterator iEnd   = basicBlock.end();

        while (iInstruction != iEnd) {
            if (! visitInstruction(* iInstruction))
                return false;

            ++iInstruction;
        }

        return true;
    }
};

class ParsedBlockVisitor {
public:
    ParsedBlockVisitor(ParsedMethod* parsedMethod) : m_parsedMethod(parsedMethod) { }
    virtual ~ParsedBlockVisitor() { }

    void run() {
        ParsedMethod::block_iterator iBlock = m_parsedMethod->blockBegin();
        const ParsedMethod::block_iterator iEnd = m_parsedMethod->blockEnd();

        while (iBlock != iEnd) {
            if (! visitBlock(** iBlock))
                break;

            ++iBlock;
        }
    }

protected:
    virtual bool visitBlock(ParsedBlock& parsedBlock) { return true; }

private:
    ParsedMethod* m_parsedMethod;
};

} // namespace st

#endif
