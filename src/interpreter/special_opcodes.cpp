#include <interpreter/special_opcodes.hpp>
#include <interpreter/usual_opcodes.hpp>
#include <interpreter/runtime.hpp>

namespace Interpreter
{

void SelfReturn::execute(Runtime& runtime, const uint16_t /*extra*/) {
    const TObject* const self = runtime.getArgumentVariable(0);
    const TContext* const currentContext = runtime.currentContext();
    runtime.setContext(currentContext->previousContext);
    if (runtime.currentContext() == runtime.nilObject()) {
        runtime.setProcessResult(self);
    } else {
        runtime.stackPush(self);
    }
}

void StackReturn::execute(Runtime& runtime, const uint16_t /*extra*/) {
    const TObject* const top = runtime.stackPop();
    const TContext* const currentContext = runtime.currentContext();
    runtime.setContext(currentContext->previousContext);
    if (runtime.currentContext() == runtime.nilObject()) {
        runtime.setProcessResult(top);
    } else {
        runtime.stackPush(top);
    }
}

void BlockReturn::execute(Runtime& runtime, const uint16_t /*extra*/) {
    const TObject* const top = runtime.stackPop();
    const TBlock* const contextAsBlock = static_cast<const TBlock* const>(runtime.currentContext());
    const TContext* const currentContext = contextAsBlock->creatingContext;
    runtime.setContext(currentContext->previousContext);
    if (runtime.currentContext() == runtime.nilObject()) {
        runtime.setProcessResult(top);
    } else {
        runtime.stackPush(top);
    }
}

void Duplicate::execute(Runtime& runtime, const uint16_t /*extra*/) {
    const TObject* const copy = runtime.stackTop();
    runtime.stackPush(copy);
}

void PopTop::execute(Runtime& runtime, const uint16_t /*extra*/) {
    runtime.stackDrop();
}

void JumpUnconditional::execute(Runtime& runtime, const uint16_t pc) {
    runtime.setPC(pc);
}

void JumpIfTrue::execute(Runtime& runtime, const uint16_t pc) {
    const TObject* const top = runtime.stackPop();
    if (top == runtime.trueObject())
        runtime.setPC(pc);
}

void JumpIfFalse::execute(Runtime& runtime, const uint16_t pc) {
    const TObject* const top = runtime.stackPop();
    if (top == runtime.falseObject())
        runtime.setPC(pc);
}

void SendToSuper::execute(Runtime& runtime, const uint16_t index) {
    const TSymbol* const selector = static_cast<const TSymbol*>( runtime.getLiteralVariable(index) );
    const TClass* const receiverClass = runtime.currentContext()->method->klass->parentClass;

    SendMessage sender;
    sender.call(runtime, selector, receiverClass);
}

} // namespace
