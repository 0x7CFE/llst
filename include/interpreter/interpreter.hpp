#pragma once

#include "runtime.hpp"
#include <map>

namespace st {
struct TSmalltalkInstruction;
}

namespace Interpreter
{

class Runtime;
class UsualOpcode;
class SpecialOpcode;
class PrimitiveOpcode;
class Interpreter
{
    std::map<int, UsualOpcode*> m_usuals;
    std::map<int, SpecialOpcode*> m_specials;
    std::map<int, PrimitiveOpcode*> m_primitives;
    Runtime m_runtime;
public:
    enum TExecuteResult {
        Failure = 2,
        BadMethod = 3,
        Success = 4,
        TimeExpired = 5
    };

    explicit Interpreter(IMemoryManager* memoryManager) : m_usuals(), m_specials(), m_primitives(), m_runtime(*this, memoryManager) {}
    explicit Interpreter(const Interpreter& interpreter):
        m_usuals(interpreter.m_usuals),
        m_specials(interpreter.m_specials),
        m_primitives(interpreter.m_primitives),
        m_runtime(*this, interpreter.m_runtime)
    {}
    Runtime& runtime();
    void execute(st::TSmalltalkInstruction instruction);
    TExecuteResult execute(TProcess* process, uint32_t ticks);
    void installUsual(int opcode, UsualOpcode* f);
    void installSpecial(int opcode, SpecialOpcode* f);
    void installPrimitive(int opcode, PrimitiveOpcode* f);
};

} // namespace
