#include <interpreter/runtime.hpp>
#include <interpreter/exceptions.hpp>

#include <cassert>
#include <stdexcept>

namespace Interpreter
{

const TClass* Runtime::getClass(const TObject* object) const
{
    return isSmallInteger(object) ? smallIntClass() : object->getClass();
}

TObject* Runtime::newOrdinaryObject(const TClass* klass, std::size_t slotSize)
{
    // Class may be moved during GC in allocation,
    // so we need to protect the pointer
    hptr<TClass> pClass = protectHptr(const_cast<TClass*>(klass));

    bool lastGCOccured = false;
    void* const objectSlot = m_memoryManager->allocate(correctPadding(slotSize), &lastGCOccured);
    if (lastGCOccured) {
        //TODO
    }
    if (!objectSlot) {
        throw out_of_memory();
        return nilObject();
    }

    // Object size stored in the TSize field of any ordinary object contains
    // number of pointers except for the first two fields
    const std::size_t fieldsCount = slotSize / sizeof(TObject*) - 2;

    TObject* const instance = new (objectSlot) TObject(fieldsCount, pClass);

    for (std::size_t index = 0; index < fieldsCount; index++)
        instance->putField(index, globals.nilObject);

    return instance;
}

TByteObject* Runtime::newBinaryObject(const TClass* klass, std::size_t dataSize) {
    // Class may be moved during GC in allocation,
    // so we need to protect the pointer
    hptr<TClass> pClass = protectHptr(const_cast<TClass*>(klass));

    // All binary objects are descendants of ByteObject
    // They could not have ordinary fields, so we may use it
    std::size_t slotSize = sizeof(TByteObject) + dataSize;

    bool lastGCOccured = false;
    void* const objectSlot = m_memoryManager->allocate(correctPadding(slotSize), &lastGCOccured);
    if (lastGCOccured) {
        //TODO
    }
    if (!objectSlot) {
        throw out_of_memory();
        return nilObject()->cast<TByteObject>();
    }

    TByteObject* instance = new (objectSlot) TByteObject(dataSize, pClass);
    return instance;
}

void Runtime::protectSlot(TObject* value, TObject** objectSlot)
{
    m_memoryManager->checkRoot(value, objectSlot);
}

void Runtime::collectGarbage()
{
    m_memoryManager->collectGarbage();
}

template<> TObjectArray* Runtime::createObject<TObjectArray>(std::size_t dataSize)
{
    return static_cast<TObjectArray*>( newOrdinaryObject(arrayClass(), sizeof(TObjectArray) + dataSize * sizeof(TObject*)) );
}

template<> TContext* Runtime::createObject<TContext>(std::size_t /*dataSize*/)
{
    return static_cast<TContext*>( newOrdinaryObject(contextClass(), sizeof(TContext)) );
}

template<> TProcess* Runtime::createObject<TProcess>(std::size_t /*dataSize*/)
{
    return static_cast<TProcess*>( newOrdinaryObject(processClass(), sizeof(TProcess)) );
}

template<> TBlock* Runtime::createObject<TBlock>(std::size_t /*dataSize*/)
{
    return static_cast<TBlock*>( newOrdinaryObject(blockClass(), sizeof(TBlock)) );
}

template<> TString* Runtime::createObject<TString>(std::size_t dataSize)
{
    return static_cast<TString*>( newBinaryObject(stringClass(), dataSize) );
}

template<> TLongInteger* Runtime::createObject<TLongInteger>(std::size_t bufferSize)
{
    return static_cast<TLongInteger*>( newBinaryObject(integerClass(), 1 + bufferSize) );
}

const Interpreter& Runtime::interpreter() const {
    return m_interpreter;
}

std::string Runtime::backtrace() const {
    std::stringstream str;

    str << "==== Process " << std::hex << std::showbase << m_currentProcess << std::dec << std::noshowbase << "\n\n";

    size_t depth = 0;
    for(const TContext* context = currentContext(); context != nilObject(); context = context->previousContext, depth++) {
        std::vector<const TObject*> args;
        for(uint32_t i = 0; i < context->arguments->getSize(); ++i) {
            args.push_back( context->arguments->getField(i) );
        }
        const TObject* receiver = args[0];
        const TClass* receiverClass = getClass(receiver);

        str << '#' << depth << " ";
        str << std::hex << std::showbase << context << std::dec << std::noshowbase;
        str << " in ";

        if (context->getClass() == blockClass()) {
            const TBlock* block = context->cast<TBlock>();
            str << "block from " << std::hex << std::showbase << block->creatingContext << std::dec << std::noshowbase;
            str << "+" << block->blockBytePointer.getValue();
        } else {
            str << receiverClass->name->toString() << ">>" << context->method->name->toString();
        }
        str << '[' << context->bytePointer.getValue() << ']';

        str << '(';
        for(uint32_t i = 0; i < args.size(); ++i) {
            const TObject* arg = args[i];
            const TClass* argClass = getClass(arg);
            if (arg == nilObject()) {
                str << "nil";
            } else if (arg == trueObject()) {
                str << "true";
            } else if (arg == falseObject()) {
                str << "false";
            } else if (argClass == smallIntClass()) {
                str << TInteger(arg).getValue();
            } else if (argClass == stringClass()) {
                const TString* stringObject = static_cast<const TString*>(arg);
                const std::string cxx_string(reinterpret_cast<const char*>(stringObject->getBytes()), stringObject->getSize());
                str << '\'' << cxx_string << '\'';
            } else {
                str << "instance of " << argClass->name->toString();
            }
            if (i != args.size() - 1)
                str << ", ";
        }
        str << ")\n";
    }
    return str.str();
}

const TObject* Runtime::getInstanceVariable(size_t index) const {
    const TObject* self = this->currentContext()->arguments->getField(0);
    return self->getField(index);
}
const TObject* Runtime::getArgumentVariable(size_t index) const {
    return this->currentContext()->arguments->getField(index);
}
const TObject* Runtime::getTemporaryVariable(size_t index) const {
    return this->currentContext()->temporaries->getField(index);
}
const TObject* Runtime::getLiteralVariable(size_t index) const {
    return this->currentContext()->method->literals->getField(index);
}
TObject** Runtime::getTemporaryPtr(size_t index) const {
    return this->currentContext()->temporaries->getFields() + index;
}
TObject** Runtime::getInstancePtr(size_t index) const {
    TObject* self = this->currentContext()->arguments->getField(0);
    return self->getFields() + index;
}
TObject* Runtime::stackTop(size_t offset) const {
    return this->currentContext()->stack->getField(this->currentContext()->stackTop - 1 - offset);
}
void Runtime::stackPush(const TObject* object) {
    //NOTE: boundary check
    //      The Timothy A. Budd's version of compiler produces
    //      bytecode which can overflow the stack of the context

    TObjectArray& stack = *this->currentContext()->stack;
    const uint32_t top = this->currentContext()->stackTop;
    const uint32_t stackSize = stack.getSize();

    if (top >= stack.getSize()) {
        hptr<TObjectArray> newStack = createHptrObject<TObjectArray>(stackSize + 7);

        for (uint32_t i = 0; i < stackSize; i++)
            newStack[i] = this->currentContext()->stack->getField(i);

        this->currentContext()->stack = newStack;
    }
    this->currentContext()->stack->putField(top, const_cast<TObject*>(object));
    this->currentContext()->stackTop += 1;
}

TObject* Runtime::stackPop() {
    if (this->currentContext()->stackTop == 0) {
        throw std::runtime_error("Runtime::stackPop: the stack is empty");
    }
    TObject* result = stackTop();
    stackDrop(1);
    return result;
}

void Runtime::stackDrop(size_t elems) {
    TInteger& stackTop = this->currentContext()->stackTop;
    while(elems--) {
        // this->currentContext()->stack->putField(stackTop - 1, const_cast<TObject*>( nilObject() ));
        stackTop -= 1;
    }
}

TObject* Runtime::nilObject() const {
    return globals.nilObject;
}
const TObject* Runtime::trueObject() const {
    return globals.trueObject;
}
const TObject* Runtime::falseObject() const {
    return globals.falseObject;
}
const TClass* Runtime::smallIntClass() const {
    return globals.smallIntClass;
}
const TClass* Runtime::blockClass() const {
    return globals.blockClass;
}
const TClass* Runtime::arrayClass() const {
    return globals.arrayClass;
}
const TClass* Runtime::stringClass() const {
    return globals.stringClass;
}
const TClass* Runtime::integerClass() const {
    return globals.integerClass;
}
const TClass* Runtime::contextClass() const {
    return globals.contextClass;
}
const TClass* Runtime::processClass() const {
    return globals.processClass;
}
const TSymbol* Runtime::badMethodSymbol() const {
    return globals.badMethodSymbol;
}
const TSymbol* Runtime::binaryMessages(binaryBuiltIns::Operator op) const {
    return static_cast<TSymbol*>( globals.binaryMessages[op] );
}


size_t Runtime::getPC() const {
    return this->currentContext()->bytePointer;
}

void Runtime::setPC(size_t pc) {
    this->currentContext()->bytePointer = pc;
}

void Runtime::setProcess(TProcess* process) {
    m_currentProcess = protectHptr(process);
}

void Runtime::setContext(TContext* context) {
    m_currentProcess->context = context;
}

TContext* Runtime::currentContext() {
    return m_currentProcess->context;
}

TContext* Runtime::currentContext() const {
    return m_currentProcess->context;
}

void Runtime::setProcessResult(const TObject* result) {
    m_currentProcess->result = const_cast<TObject*>(result);
}

TMethod* Runtime::lookupMethod(const TSymbol* selector, const TClass* klass) {
    // Scanning through the class hierarchy from the klass up to the Object
    for (const TClass* currentClass = klass; currentClass != nilObject(); currentClass = currentClass->parentClass) {
        assert(currentClass != 0);
        TDictionary* methods = currentClass->methods;
        TMethod* method = methods->find<TMethod>(selector);
        if (method) {
            return method;
        }
    }

    return 0;
}

} // namespace
