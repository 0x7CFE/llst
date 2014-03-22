#ifndef LLST_INSTRUCTIONS_INCLUDED
#define LLST_INSTRUCTIONS_INCLUDED

#include <assert.h>
#include <stdint.h>
#include <vector>
#include <list>
#include <map>

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
        m_argument = static_cast<TArgument>((bytecode >> 8) & 0xFF);
        m_extra    = static_cast<TExtra>((bytecode >> 16) & 0xFF);
    }

    // Decode instruction from method bytecode
    // Shifts bytePointer to the next instruction
    TSmalltalkInstruction(const TByteObject& byteCodes, uint16_t& bytePointer);

    TOpcode getOpcode() const { return m_opcode; }
    TArgument getArgument() const { return m_argument; }
    TExtra getExtra() const { return m_extra; }

    // Return fixed width representation of bytecode suitable for storing in arrays
    TUnpackedBytecode serialize() const {
        return static_cast<uint8_t>(m_opcode) | (m_argument << 8) | (m_extra << 16);
    }

private:
    TOpcode   m_opcode;
    TArgument m_argument;
    TExtra    m_extra;
};

class BasicBlock {
public:
    typedef std::vector<TSmalltalkInstruction::TUnpackedBytecode> TInstructionVector;

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

private:
    TInstructionVector m_instructions;
};

class ParsedMethod {
public:
    typedef std::list<BasicBlock*> TBasicBlockList;
    typedef TBasicBlockList::iterator iterator;
    iterator begin() { return m_basicBlocks.begin(); }
    iterator end() { return m_basicBlocks.end(); }

    BasicBlock* createBasicBlock() {
        m_basicBlocks.push_back(new BasicBlock);
        return m_basicBlocks.back();
    }

    ParsedMethod(TMethod* method);
    ParsedMethod() {}

    ~ParsedMethod() {
        for (TBasicBlockList::iterator iBlock = m_basicBlocks.begin(),
            end = m_basicBlocks.end(); iBlock != end; ++iBlock)
        {
            delete * iBlock;
        }
    }

private:
    void parse(TMethod* method);

private:
    TBasicBlockList m_basicBlocks;

    typedef std::map<uint16_t, BasicBlock*> TOffsetToBasicBlockMap;
    TOffsetToBasicBlockMap m_offsetToBasicBlock;
};

class BasicBlockVisitor {
public:
    BasicBlockVisitor(ParsedMethod* parsedMethod) : m_parsedMethod(parsedMethod) { }
    virtual ~BasicBlockVisitor() { }

    virtual bool visitBlock(BasicBlock& basicBlock) { return true; }

    void run() {
        ParsedMethod::iterator iBlock = m_parsedMethod->begin();
        const ParsedMethod::iterator iEnd = m_parsedMethod->end();

        while (iBlock != iEnd) {
            if (! visitBlock(** iBlock))
                break;

            ++iBlock;
        }
    }

private:
    ParsedMethod* m_parsedMethod;
};

class InstructionVisitor : public BasicBlockVisitor {
public:
    InstructionVisitor(ParsedMethod* parsedMethod) : BasicBlockVisitor(parsedMethod) { }
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

} // namespace st

#endif
