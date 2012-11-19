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
    
    TArray* temporaries = 0;
    TArray* instanceVariables = 0;
    TArray* arguments = 0;
    TArray* literals = 0;
    
    while (true) {
        if (ticks && (--ticks == 0)) {
            // Time frame expired
            // TODO
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
            case pushInstance:
                if (!arguments)
                    arguments = context->arguments;
                if (!instanceVariables)
                    instanceVariables = arguments[0];
                stack[stackTop++] = instanceVariables[instruction.low];  // FIXME
                break;
                
            case pushArgument:
                if (!arguments)
                    arguments = context->arguments;
                stack[stackTop++] = arguments[instruction.low];
                break;
                
            case pushTemporary:
                if (!temporaries)
                    temporaries = context->temporaries;
                stack[stackTop++] = temporaries[instruction.low];
                break;
                
            case pushLiteral:
                if (!literals)
                    literals = method->literals;
                stack[stackTop++] = literals[instruction.low];
                break;
                
            case pushConstant:
                switch (instruction.low) {
                    case 0: case 1: 
                    case 2: case 3: 
                    case 4: case 5: 
                    case 6: case 7: 
                    case 8: case 9: 
                        stack[stackTop++] = (TObject*) newInteger(instruction.low);
                        break;
                        
                    case nilConst:   stack[stackTop++] = globals.nilObject;   break;
                    case trueConst:  stack[stackTop++] = globals.trueObject;  break;
                    case falseConst: stack[stackTop++] = globals.falseObject; break;
                    default:
                        /* TODO unknown push constant */ ;
                }
                break;
                
            case assignInstance:
                
                break;
                
            case assignTemporary:
                if (!temporaries)
                    temporaries = context->temporaries;
                temporaries[instruction.low] = stack[stackTop - 1];
                break;
                        
            case markArguments:
                rootStack.push_back(context);
                arguments = newObject<TArray>(instruction.low);
                arguments->setClass(globals.arrayClass);
                while (instruction.low > 0)
                    arguments[--instruction.low] = stack[--stackTop];
                stack[stackTop++] = arguments;
                arguments = 0;
                break;
                
            case sendMessage:
                if (!literals)
                    literals = method->literals;
                TObject* messageSelector = literals[instruction.low];
                arguments = stack[--stackTop];
                
                break;
        }
    }
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


