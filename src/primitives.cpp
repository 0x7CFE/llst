#include <primitives.h>

#include <memory.h>
#include <opcodes.h>
#include <stdio.h>
#include <stdlib.h>

TObject* callPrimitive(uint8_t opcode, TObjectArray* arguments, bool& primitiveFailed) {
    primitiveFailed = false;
    TObjectArray& args = *arguments;
    
    switch (opcode)
    {
        case primitive::objectsAreEqual: { // 1
            TObject* right = args[1];
            TObject* left  = args[0];
            
            if (left == right)
                return globals.trueObject;
            else
                return globals.falseObject;
        } break;
        
        case primitive::getClass: { // 2
            TObject* object = args[0];
            return isSmallInteger(object) ? globals.smallIntClass : object->getClass();
        } break;
        
        case primitive::getSize: { // 4
            TObject* object     = args[0];
            uint32_t objectSize = isSmallInteger(object) ? 0 : object->getSize();
            
            return reinterpret_cast<TObject*>(newInteger(objectSize));
        } break;
        
        default: {
            primitiveFailed = true;
            fprintf(stderr, "Unimplemented or invalid primitive %d\n", opcode);
            //exit(1);
        }
    }
    return globals.nilObject;
}