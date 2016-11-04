#include <interpreter/interpreter.hpp>
#include <interpreter/opcodes.hpp>
#include <interpreter/exceptions.hpp>

#include <instructions.h>

#include <iostream>

namespace Interpreter
{

void Interpreter::installUsual(int opcode, UsualOpcode* f) {
    m_usuals.insert(std::make_pair(opcode, f));
}
void Interpreter::installSpecial(int opcode, SpecialOpcode* f) {
    m_specials.insert(std::make_pair(opcode, f));
}
void Interpreter::installPrimitive(int opcode, PrimitiveOpcode* f) {
    m_primitives.insert(std::make_pair(opcode, f));
}
Runtime& Interpreter::runtime() {
    return m_runtime;
}

void Interpreter::execute(st::TSmalltalkInstruction instruction)
{
    switch (instruction.getOpcode()) {
        case opcode::doSpecial: {
            std::map<int, SpecialOpcode*>::const_iterator it = m_specials.find(instruction.getArgument());
            if (it != m_specials.end()) {
                it->second->execute(m_runtime, instruction.getExtra());
            } else {
                std::stringstream error;
                error << "No handler for special instruction '" << instruction.toString() << "'";
                throw std::runtime_error(error.str());
            }
        } break;
        case opcode::doPrimitive: {
            std::map<int, PrimitiveOpcode*>::const_iterator it = m_primitives.find(instruction.getExtra());
            if (it != m_primitives.end()) {
                it->second->execute(m_runtime, instruction.getArgument());
            } else {
                std::stringstream error;
                error << "No handler for primitive '" << instruction.toString() << "'";
                throw std::runtime_error(error.str());
            }
        } break;
        default: {
            std::map<int, UsualOpcode*>::const_iterator it = m_usuals.find(instruction.getOpcode());
            if (it != m_usuals.end()) {
                it->second->execute(m_runtime, instruction.getArgument(), instruction.getExtra());
            } else {
                std::stringstream error;
                error << "No handler for usual instruction '" << instruction.toString() << "'";
                throw std::runtime_error(error.str());
            }
        }
    }
}

Interpreter::TExecuteResult Interpreter::execute(TProcess* process, uint32_t ticks) {
    m_runtime.setProcess(process);

    while (m_runtime.currentContext() != m_runtime.nilObject()) {
        const TByteObject& byteCodes = * m_runtime.currentContext()->method->byteCodes;
        uint16_t currentPC = m_runtime.getPC();

        const st::TSmalltalkInstruction instruction = st::InstructionDecoder::decodeAndShiftPointer( byteCodes, currentPC );
        m_runtime.setPC(currentPC);
        try {
            this->execute(instruction);
        } catch(const out_of_memory& e) {
            std::cerr << e.what() << std::endl;
            // try to unwind the stack
            // TODO
            /*
            TContext* current = m_runtime.currentContext();
            TContext* previous = current->previousContext;
            for (; previous != m_runtime.nilObject(); current = previous, previous = current->previousContext) {
                current->stack;
            }
            m_runtime.collectGarbage();
            */
            return Failure;
        } catch(const halt_execution&) {
            return Failure;
        } catch(const std::exception& e) {
            std::cerr << e.what() << std::endl;
            std::cerr << "Backtrace: \n" << m_runtime.backtrace();
            return Failure;
        }

        if (ticks && (--ticks == 0)) {
            // Time frame expired
            return TimeExpired;
        }
    }
    return Success;
}

} // namespace
