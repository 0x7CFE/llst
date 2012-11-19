#include <vm.h>
#include <string.h>

int SmalltalkVM::compareSymbols(const TByteObject* left, const TByteObject* right)
{
    // This function compares two byte objects depending on their lenght and contents
   
    uint32_t leftSize = left->getSize();
    uint32_t rightSize = right->getSize();
    uint32_t minSize = leftSize;
    
    if (rightSize < minSize)
        minSize = rightSize;
    
//    return memcmp(left->getBytes(), right->getBytes(), minSize);
    
    // Comparing the byte string symbol by symbol
    for (uint32_t i = 0; i < minSize; i++) {
        if (left[i] != right[i])
            return left[i] - right[i];
    }
    
    return leftSize - rightSize;
}

TMethod* SmalltalkVM::lookupMethod(const TObject* selector, const TClass* klass)
{
    //Scanning through the class hierarchy from the klass up to the Object
    for (; klass != globals.nilObject; klass = klass->parentClass) {
        TDictionary* dictionary = klass->methods;
        TArray* keys   = dictionary->keys;
        TArray* values = dictionary->values;
        
        // keys are stored in order
        // thus we may apply binary search
        
        uint32_t low = 0;
        uint32_t high = keys->getSize();
        
        while (low < high) {
            uint32_t mid = (low + high) / 2;
            TObject* key = keys[mid];
            
            if (key == selector)
                return (TMethod*) values[mid];
            
            if (compareSymbols((TByteObject*) selector, (TByteObject*) key) < 0)
                high = mid;
            else 
                low = mid + 1;
        }
    }
    
    return NULL;
}

void SmalltalkVM::flushCache()
{
    for (size_t i = 0; i < LOOKUP_CACHE_SIZE; i++)
        m_lookupCache[i].name = 0;
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
    rootStack.push_back(process);
    
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
    
    TObject* returedValue = globals.nilObject;
    
    while (true) {
        if (ticks && (--ticks == 0)) {
            // Time frame expired
            TProcess* newProcess = rootStack.back(); rootStack.pop_back();
            newProcess->context = context;
            newProcess->result = returedValue;
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
                
                rootStack.push_back(context);
                TArray* args = newObject<TArray>(instruction.low);
                args->setClass(globals.arrayClass);
                
                for (int index = instruction.low - 1; index > 0; index--)
                    args[index] = stack[--stackTop];
                
                stack[stackTop++] = args;
            } break;
                
            case sendMessage: doSendMessage(context, method, stackTop); break;
            
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
            
        case nilConst:   stack[stackTop++] = globals.nilObject;   break;
        case trueConst:  stack[stackTop++] = globals.trueObject;  break;
        case falseConst: stack[stackTop++] = globals.falseObject; break;
        default:
            /* TODO unknown push constant */ ;
    }
}

void SmalltalkVM::doSendMessage(TContext* context, TMethod* method, uint32_t& stackTop)
{
    TObject* messageSelector = literals[instruction.low];
    context->arguments = context->stack[--stackTop];
    
    break;
}

void* TObject::operator new(size_t size)
{
    // TODO allocate the object on the GC heap
    return llvm_gc_allocate(size);
}

template<class T> T* newObject(TClass* klass, size_t objectSize /*= 0*/)
{
    size_t baseSize = sizeof T;
    void* objectSlot = llvm_gc_allocate(baseSize + objectSize * 4);
    TObject* instance = new (objectSlot) T;
    instance->setClass(klass);
    instance->construct(objectSize);
    
    return instance;
}


