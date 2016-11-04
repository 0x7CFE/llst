#pragma once

#include <interpreter/opcodes.hpp>
#include <types.h>

namespace Interpreter
{

class Runtime;

class UsualOpcodeWithExtra : public UsualOpcode {
public:
    virtual void execute(Runtime& runtime, const uint8_t arg, const uint16_t extra) = 0;
};

class UsualOpcodeOnlyArg : public UsualOpcode {
public:
    virtual void execute(Runtime& runtime, const uint8_t arg, const uint16_t extra);
    virtual void execute(Runtime& runtime, const uint8_t arg) = 0;
};




class PushInstanceVariable : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t index);
};

class PushArgumentVariable : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t index);
};

class PushTemporaryVariable : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t index);
};

class PushLiteralVariable : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t index);
};

class PushInlineConstant : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t constant);
};

class AssignTemporaryVariable : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t index);
};

class AssignInstanceVariable : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t index);
};

class ArrayPack : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t size);
    void call(Runtime& runtime, const uint8_t size);
};

class SendMessage : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t index);
    void call(Runtime& runtime, const TSymbol* selector);
    void call(Runtime& runtime, const TSymbol* selector, const TClass* receiverClass);
};

class SendUnaryMessage : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t opcode);
};

class SendBinaryMessage : public UsualOpcodeOnlyArg {
public:
    virtual void execute(Runtime& runtime, const uint8_t opcode);
};

class PushBlock : public UsualOpcodeWithExtra {
public:
    virtual void execute(Runtime& runtime, const uint8_t argumentLocation, const uint16_t pc);
};

} // namespace
