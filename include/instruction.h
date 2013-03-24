#ifndef LLST_INSTRUCTION_H_INCLUDED
#define LLST_INSTRUCTION_H_INCLUDED

#include <string>
#include <sstream>
#include <stdexcept>
#include <opcodes.h>
#include <stdint.h>
// TInstruction represents one decoded Smalltalk instruction.
// Actual meaning of parts is determined during the execution.
struct TInstruction {
    uint8_t low;
    uint8_t high;
    
    std::string toString();
};

#endif