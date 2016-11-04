#pragma once

#include <interpreter/opcodes.hpp>
#include <types.h>

namespace Interpreter
{

class Runtime;

class SelfReturn : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t extra);
};

class StackReturn : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t extra);
};

class BlockReturn : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t extra);
};

class Duplicate : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t extra);
};

class PopTop : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t extra);
};

class JumpUnconditional : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t pc);
};

class JumpIfTrue : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t pc);
};

class JumpIfFalse : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t pc);
};

class SendToSuper : public SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t index);
};

} // namespace
