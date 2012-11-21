#include <vm.h>
#include <string.h>
#include <stdlib.h>

TMethod* SmalltalkVM::lookupMethodInCache(TObject* selector, TClass* klass)
{
    uint32_t hash = reinterpret_cast<uint32_t>(selector) ^ reinterpret_cast<uint32_t>(klass);
    TMethodCacheEntry& entry = m_lookupCache[hash % LOOKUP_CACHE_SIZE];
    if (entry.methodName == selector && entry.receiverClass == klass) {
        m_cacheHits++;
        return entry.method;
    } else {
        m_cacheMisses++;
        return 0;
    }
}

TMethod* SmalltalkVM::lookupMethod(const TSymbol* selector, const TClass* klass)
{
    // First of all checking the method cache
    // Frequently called methods most likely will be there
    TObject* result = lookupMethodInCache(selector, klass);
    if (result)
        return result; // We're lucky!
    
    // Well, maybe we'll be luckier next time. For now we need to do the full search.
    // Scanning through the class hierarchy from the klass up to the Object
    for (TClass* currentClass = klass; currentClass != Image::globals.nilObject; currentClass = currentClass->parentClass) {
        TDictionary* methods = currentClass->methods;
        result = (TMethod*) methods->find(selector);
        if (result)
            return result;
    }
    
    return 0;
}

void SmalltalkVM::flushCache()
{
    for (size_t i = 0; i < LOOKUP_CACHE_SIZE; i++)
        m_lookupCache[i].methodName = 0;
}

// uint32_t getIntegerValue(const TInteger* value)
// {
//     uint32_t integer = reinterpret_cast<uint32_t>(value);
//     if (integer & 1 == 1)
//         return integer >> 1;
//     else {
//         // TODO get the value from boxed object
//         return 0;
//     }
// }

TInstruction decodeInstruction(TByteObject* byteCodes, uint32_t bytePointer)
{
    TInstruction result;
//    result.low = (result.high = byteCodes[bytePointer++])
}

int SmalltalkVM::execute(TProcess* process, uint32_t ticks)
{
    m_rootStack.push_back(process);
    
    // current execution context and the executing method
    TContext* context = process->context;
    TMethod*  method  = context->method;
    
    TByteObject* byteCodes   = method->byteCodes;
    uint32_t     bytePointer = getIntegerValue(context->bytePointer);
    
    TArray*  stack    = context->stack;
    uint32_t stackTop = getIntegerValue(context->stackTop);
    
    TArray* temporaries = context->temporaries;
    TArray* arguments = context->arguments;
    TArray* instanceVariables = arguments[0];
    TArray* literals = method->literals;
    
    TObject* returnedValue = m_image.globals.nilObject;
    
    while (true) {
        if (ticks && (--ticks == 0)) {
            // Time frame expired
            TProcess* newProcess = m_rootStack.back(); m_rootStack.pop_back();
            newProcess->context = context;
            newProcess->result = returnedValue;
            context->bytePointer = newInteger(bytePointer);
            context->stackTop = newInteger(stackTop);
            return returnTimeExpired;
        }
            
        // decoding the instruction
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == 0) { // TODO extended constant
            instruction.high = instruction.low;
            instruction.low = byteCodes[bytePointer++];
        }
        
        switch (instruction.high) {
            case pushInstance:    stack[stackTop++] = instanceVariables[instruction.low]; break;
            case pushArgument:    stack[stackTop++] = arguments[instruction.low];         break;
            case pushTemporary:   stack[stackTop++] = temporaries[instruction.low];       break;
            case pushLiteral:     stack[stackTop++] = literals[instruction.low];          break;
            case assignTemporary: temporaries[instruction.low] = stack[stackTop - 1];     break;
            
            case assignInstance:
                instanceVariables[instruction.low] = stack[stackTop - 1];
                // TODO isDynamicMemory()
                break;
                
            case pushConstant: 
                doPushConstant(instruction.low, TArray* stack, uint32_t& stackTop); 
                break;
                
            case pushBlock:
                
                break;
                
            case markArguments: {
                // This operation takes instruction.low arguments 
                // from the top of the stack and creates new array with them
                
                m_rootStack.push_back(context);
                TArray* args = newObject<TArray>(instruction.low);
                
                for (int index = instruction.low - 1; index > 0; index--)
                    args[index] = stack[--stackTop];
                
                stack[stackTop++] = args;
            } break;
                
            case sendMessage: 
                doSendMessage(method->literals[instruction.low], stack[--stackTop], context, stackTop); 
                break;
            
        }
    }
}


void SmalltalkVM::doPushConstant(uint8_t constant, TArray* stack, uint32_t& stackTop)
{
    switch (constant) {
        case 0: 
        case 1: 
        case 2: 
        case 3: 
        case 4: 
        case 5: 
        case 6: 
        case 7: 
        case 8: 
        case 9: 
            stack[stackTop++] = (TObject*) newInteger(constant);
            break;
            
        case nilConst:   stack[stackTop++] = Image::globals.nilObject;   break;
        case trueConst:  stack[stackTop++] = Image::globals.trueObject;  break;
        case falseConst: stack[stackTop++] = Image::globals.falseObject; break;
        default:
            /* TODO unknown push constant */ ;
    }
}

void SmalltalkVM::doSendMessage(TObject* selector, TArray* arguments, TContext* context, uint32_t& stackTop)
{
    TClass*  receiverClass    = arguments[0];
    
    TMethod* method = lookupMethod(selector, receiverClass);
    if (! method) {
        // Oops. Nothing was found.
        // Seems that current object does not understand this message
        
        if (selector == m_image.globals.badMethodSymbol) {
            // Something really bad happened
            // TODO error
            exit(1);
        }
    }
    
    break;
}

template<class T> T* SmalltalkVM::newObject(size_t objectSize /*= 0*/)
{
    // TODO fast access to common classes
    TClass* klass = m_image.getGlobal(T::className());
    if (!klass)
        return Image::globals.nilObject;
    
    size_t baseSize = sizeof T;
    void* objectSlot = llvm_gc_allocate(baseSize + objectSize * 4);
    if (!objectSlot)
        return Image::globals.nilObject;
    
    // FIXME compute size correctly depending on object type
    uint32_t trueSize = baseSize + objectSize;
    TObject* instance = (TObject*) new (objectSlot) T(klass, trueSize);
    return instance;
}


