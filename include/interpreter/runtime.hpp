#pragma once

#include <types.h>
#include <opcodes.h>
#include <memory.h>

namespace Interpreter
{

class Interpreter;
class Runtime {
private:
    const Interpreter& m_interpreter;
    IMemoryManager* m_memoryManager;
    hptr<TProcess> m_currentProcess;
public:
    Runtime(const Interpreter& interpreter, IMemoryManager* memoryManager) :
        m_interpreter(interpreter),
        m_memoryManager(memoryManager),
        m_currentProcess(nilObject()->cast<TProcess>(), m_memoryManager)
    {
    }
    Runtime(const Interpreter& interpreter, const Runtime& runtime) :
        m_interpreter(interpreter),
        m_memoryManager(runtime.m_memoryManager),
        m_currentProcess(nilObject()->cast<TProcess>(), m_memoryManager)
    {
    }

    const Interpreter& interpreter() const;
    std::string backtrace() const;

    const TObject* getInstanceVariable(size_t index) const;
    const TObject* getArgumentVariable(size_t index) const;
    const TObject* getTemporaryVariable(size_t index) const;
    const TObject* getLiteralVariable(size_t index) const;
    TObject** getTemporaryPtr(size_t index) const;
    TObject** getInstancePtr(size_t index) const;

    TObject* stackTop(size_t offset = 0) const;
    void stackPush(const TObject* object);
    TObject* stackPop();
    void stackDrop(size_t elems = 1);

    TObject* nilObject() const;
    const TObject* trueObject() const;
    const TObject* falseObject() const;
    const TClass* smallIntClass() const;
    const TClass* blockClass() const;
    const TClass* arrayClass() const;
    const TClass* stringClass() const;
    const TClass* integerClass() const;
    const TClass* contextClass() const;
    const TClass* processClass() const;
    const TSymbol* badMethodSymbol() const;
    const TSymbol* binaryMessages(binaryBuiltIns::Operator op) const;

    size_t getPC() const;
    void setPC(size_t pc);
    void setProcess(TProcess* process);
    void setContext(TContext* context);
    TContext* currentContext();
    TContext* currentContext() const;
    void setProcessResult(const TObject* result);
    TMethod* lookupMethod(const TSymbol* selector, const TClass* klass);

    const TClass* getClass(const TObject* object) const;
    TObject* newOrdinaryObject(const TClass* klass, std::size_t slotSize);
    TByteObject* newBinaryObject(const TClass* klass, std::size_t dataSize);
    void protectSlot(TObject* value, TObject** objectSlot);
    void collectGarbage();

    template<class T> T* createObject(std::size_t dataSize = 0);
    template<class T> hptr<T> protectHptr(T* object) { return hptr<T>(object, m_memoryManager); }
    template<class T> hptr<T> createHptrObject(std::size_t dataSize = 0) { return protectHptr<T>(createObject<T>(dataSize)); }
};

} // namespace
