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
    
    return memcmp(left->getBytes(), right->getBytes(), minSize);
    
//     // Comparing the byte string symbol by symbol
//     for (uint32_t i = 0; i < minSize; i++)
//     {
//         if (left[i] != right[i])
//             return left[i] - right[i];
//     }
//     
//     return leftSize - rightSize;
}

TObject* SmalltalkVM::lookupMethod(const TObject* selector, const TClass* klass)
{
    //Scanning through the class hierarchy from the klass up to the Object
    for (; klass != m_globals.nilObject; klass = klass->parentClass) {
        TDictionary* dictionary = klass->methods;
        TObject* keys = dictionary->keys;
        TObject* values = dictionary->values;
        
        // keys are stored in order
        // thus we may apply binary search
        
        uint32_t low = 0;
        uint32_t high = keys->getSize();
        
        while (low < high) {
            uint32_t mid = (low + high) / 2;
            TObject* key = keys[mid];
            
            if (key == selector)
                return values[mid];
            
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
    TMethod* method = context->method;
    
    TByteObject* byteCodes = method->byteCodes;
    uint32_t bytePointer =  getIntegerValue(context->bytePointer);
    
    TObject* stack = context->stack;
    uint32_t stackTop = getIntegerValue(context->stackTop);
    
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
                TObject* instanceVariables = context->arguments[0];
                stack[stackTop++] = instanceVariables[instruction.low];  // FIXME
                break;
        }
    }
}

