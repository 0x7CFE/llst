#include <primitives.h>

#include <memory.h>
#include <opcodes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

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
        
        case primitive::ioPutChar: { // 3
            TInteger charObject = reinterpret_cast<TInteger>(args[0]);
            int8_t   charValue  = getIntegerValue(charObject);

            putchar(charValue);
            return globals.nilObject;
        } break;
        
        case primitive::ioGetChar: { // 9
            int32_t input = getchar();
            
            if (input == EOF)
                return globals.nilObject;
            else
                return reinterpret_cast<TObject*>(newInteger(input));

        } break;
        
        case primitive::stringAt:      // 21
        case primitive::stringAtPut: { // 22
            TObject* indexObject = 0;
            TString* string      = 0;
            TObject* valueObject = 0;
            
            // If the method is String:at:put then pop a value from the stack
            if (opcode == primitive::stringAtPut) {
                indexObject = args[2];
                string      = (TString*) args[1];
                valueObject = args[0];
            } else { // String:at:put
                indexObject = args[1];
                string      = (TString*) args[0];
                //valueObject is not used in primitive stringAtPut
            }
            
            if (! isSmallInteger(indexObject)) {
                primitiveFailed = true;
                break;
            }
            
            // Smalltalk indexes arrays starting from 1, not from 0
            // So we need to recalculate the actual array index before
            uint32_t actualIndex = getIntegerValue(reinterpret_cast<TInteger>(indexObject)) - 1;
            
            // Checking boundaries
            if (actualIndex >= string->getSize()) {
                primitiveFailed = true;
                break;
            }
            
            if (opcode == primitive::stringAt)
                // String:at
                return reinterpret_cast<TObject*>(newInteger( string->getByte(actualIndex) ));
            else {
                // String:at:put
                TInteger value = reinterpret_cast<TInteger>(valueObject);
                string->putByte(actualIndex, getIntegerValue(value));
                return (TObject*) string;
            }
        } break;
        
        case primitive::smallIntAdd:        // 10
        case primitive::smallIntDiv:        // 11
        case primitive::smallIntMod:        // 12
        case primitive::smallIntLess:       // 13
        case primitive::smallIntEqual:      // 14
        case primitive::smallIntMul:        // 15
        case primitive::smallIntSub:        // 16
        case primitive::smallIntBitOr:      // 36
        case primitive::smallIntBitAnd:     // 37
        case primitive::smallIntBitShift: { // 39
            // Loading operand objects
            TObject* rightObject = args[1];
            TObject* leftObject  = args[0];
            if ( !isSmallInteger(leftObject) || !isSmallInteger(rightObject) ) {
                primitiveFailed = true;
                break;
            }
            
            // Extracting values
            int32_t leftOperand  = getIntegerValue(reinterpret_cast<TInteger>(leftObject));
            int32_t rightOperand = getIntegerValue(reinterpret_cast<TInteger>(rightObject));
            
            // Performing an operation
            return callSmallIntPrimitive(opcode, leftOperand, rightOperand, primitiveFailed);
        } break;
        
        // FIXME opcodes 253-255 are not standard
        case primitive::getSystemTicks: { //253
            timeval tv;
            gettimeofday(&tv, NULL);
            return reinterpret_cast<TObject*>(newInteger( (tv.tv_sec*1000000 + tv.tv_usec) / 1000));
        } break;
        
        default: {
            primitiveFailed = true;
            fprintf(stderr, "Unimplemented or invalid primitive %d\n", opcode);
            //exit(1);
        }
    }
    return globals.nilObject;
}

TObject* callSmallIntPrimitive(uint8_t opcode, int32_t leftOperand, int32_t rightOperand, bool& primitiveFailed) {
    switch (opcode) {
        case primitive::smallIntAdd:
            return reinterpret_cast<TObject*>(newInteger( leftOperand + rightOperand )); //FIXME possible overflow
        
        case primitive::smallIntDiv:
            if (rightOperand == 0) {
                primitiveFailed = true;
                return globals.nilObject;
            }
            return reinterpret_cast<TObject*>(newInteger( leftOperand / rightOperand ));
        
        case primitive::smallIntMod:
            if (rightOperand == 0) {
                primitiveFailed = true;
                return globals.nilObject;
            }
            return reinterpret_cast<TObject*>(newInteger( leftOperand % rightOperand ));
        
        case primitive::smallIntLess:
            if (leftOperand < rightOperand)
                return globals.trueObject;
            else
                return globals.falseObject;
        
        case primitive::smallIntEqual:
            if (leftOperand == rightOperand)
                return globals.trueObject;
            else
                return globals.falseObject;
        
        case primitive::smallIntMul:
            return reinterpret_cast<TObject*>(newInteger( leftOperand * rightOperand )); //FIXME possible overflow
        
        case primitive::smallIntSub:
            return reinterpret_cast<TObject*>(newInteger( leftOperand - rightOperand )); //FIXME possible overflow
        
        case primitive::smallIntBitOr:
            return reinterpret_cast<TObject*>(newInteger( leftOperand | rightOperand ));
        
        case primitive::smallIntBitAnd:
            return reinterpret_cast<TObject*>(newInteger( leftOperand & rightOperand ));
        
        case primitive::smallIntBitShift: {
            // operator << if rightOperand < 0, operator >> if rightOperand >= 0
            
            int32_t result = 0;
            
            if (rightOperand < 0) {
                //shift right
                result = leftOperand >> -rightOperand;
            } else {
                // shift left ; catch overflow
                result = leftOperand << rightOperand;
                if (leftOperand > result) {
                    primitiveFailed = true;
                    return globals.nilObject;
                }
            }
            
            return reinterpret_cast<TObject*>(newInteger(result));
        }
        
        default:
            fprintf(stderr, "Invalid SmallInt opcode %d\n", opcode);
            exit(1);
    }
}