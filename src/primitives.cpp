#include <primitives.h>

#include <memory.h>
#include <opcodes.h>
#include <cstdlib>
#include <sys/time.h>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

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
        
        case primitive::stringAt:      // 21
        case primitive::stringAtPut: { // 22
            TObject* indexObject = 0;
            TString* string      = 0;
            TObject* valueObject = 0;
            
            // If the method is String:at:put then pop a value from the stack
            if (opcode == primitive::stringAtPut) {
                indexObject = args[2];
                string      = args.getField<TString>(1);
                valueObject = args[0];
            } else { // String:at:put
                indexObject = args[1];
                string      = args.getField<TString>(0);
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

        case primitive::ioGetChar:          // 9
        case primitive::ioPutChar:          // 3
        case primitive::ioFileOpen:         // 100
        case primitive::ioFileClose:        // 103
        case primitive::ioFileSetStatIntoArray:   // 105
        case primitive::ioFileReadIntoByteArray:  // 106
        case primitive::ioFileWriteFromByteArray: // 107
        case primitive::ioFileSeek: {        // 108
            
            return callIOPrimitive(opcode, args, primitiveFailed);
            
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
            std::fprintf(stderr, "Unimplemented or invalid primitive %d\n", opcode);
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
            std::fprintf(stderr, "Invalid SmallInt opcode %d\n", opcode);
            std::exit(1);
    }
}

TObject* callIOPrimitive(uint8_t opcode, TObjectArray& args, bool& primitiveFailed) {
    switch (opcode) {
        
        case primitive::ioGetChar: { // 9
            int32_t input = std::getchar();
            
            if (input == EOF)
                return globals.nilObject;
            else
                return reinterpret_cast<TObject*>(newInteger(input));
        } break;
        
        case primitive::ioPutChar: { // 3
            TInteger charObject = reinterpret_cast<TInteger>(args[0]);
            int8_t   charValue  = getIntegerValue(charObject);

            std::putchar(charValue);
        } break;
        
        case primitive::ioFileOpen: { // 100
            TString* name = args.getField<TString>(0);
            int32_t  mode = getIntegerValue(reinterpret_cast<TInteger>( args[1] ));
            
            //We have to pass NULL-terminated string to open()
            //The easiest way is to build it with std::string
            std::string filename((char*) name->getBytes(), name->getSize());
            
            int32_t fileID = open(filename.c_str(), mode, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP );
            if (fileID < 0) {
                primitiveFailed = true;
            } else {
                return reinterpret_cast<TObject*>( newInteger(fileID) );
            }
        } break;
        
        case primitive::ioFileClose: { // 103
            int32_t fileID = getIntegerValue(reinterpret_cast<TInteger>( args[0] ));
            
            int32_t result = close(fileID);
            if (result < 0) {
                primitiveFailed = true;
            }
        } break;
        
        case primitive::ioFileSetStatIntoArray: { // 105
            int32_t fileID = getIntegerValue(reinterpret_cast<TInteger>( args[0] ));
            TObjectArray* array = args.getField<TObjectArray>(1);
            
            struct stat fileStat;
            if( fstat(fileID, &fileStat) < 0 ) {
                primitiveFailed = true;
            } else {
                TObject* size = reinterpret_cast<TObject*>( newInteger( fileStat.st_size ));
                array->putField(0, size);
                
                TObject* inode = reinterpret_cast<TObject*>( newInteger( fileStat.st_ino ));
                array->putField(1, inode);
                
                TObject* modeMask = reinterpret_cast<TObject*>( newInteger( fileStat.st_mode ));
                array->putField(2, modeMask);
            }
        } break;
        
        case primitive::ioFileReadIntoByteArray:    // 106
        case primitive::ioFileWriteFromByteArray: { // 107
            int32_t fileID = getIntegerValue(reinterpret_cast<TInteger>( args[0] ));
            TByteArray* bufferArray = args.getField<TByteArray>(1);
            uint32_t size = getIntegerValue(reinterpret_cast<TInteger>( args[2] ));
            
            if ( size > bufferArray->getSize() ) {
                primitiveFailed = true;
                break;
            }
            
            int32_t involvedItems;
            
            if (opcode == primitive::ioFileReadIntoByteArray) {
                involvedItems = read(fileID, bufferArray->getBytes(), size);
            } else { // ioFileWriteFromByteArray
                involvedItems = write(fileID, bufferArray->getBytes(), size);
            }
            
            if (involvedItems < 0) {
                primitiveFailed = true;
            } else {
                return reinterpret_cast<TObject*>(newInteger(involvedItems));
            }
            
        } break;
        case primitive::ioFileSeek: { // 108
            int32_t   fileID = getIntegerValue(reinterpret_cast<TInteger>( args[0] ));
            int32_t position = getIntegerValue(reinterpret_cast<TInteger>( args[1] ));
            
            if( (position < 0) || ((position = lseek(fileID, position, SEEK_SET)) < 0) ) {
                primitiveFailed = true;
            } else {
                return reinterpret_cast<TObject*>(newInteger(position));
            }
        } break;
        
        default:
            std::fprintf(stderr, "Invalid IO opcode %d\n", opcode);
            std::exit(1);
            
    }
    return globals.nilObject;
}
