#include <vm.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>

TObject* Image::getGlobal(const char* name)
{
    TDictionary* globalsDictionary = globals.globalsObject;
    TObject* result = globalsDictionary->find(name);
    return result;
}

bool Image::openImageFile(const char* fileName)
{
    // Opening file for reading 
    imageFileFD = open(fileName, O_RDONLY);
    if (imageFileFD < 0)
        return false;
    
    // Reading file size in bytes
    struct stat st;
    if (fstat(imageFileFD, &st) < 0) {
        close(imageFileFD);
        imageFileFD = -1;
        return false;
    }
    imageFileSize = st.st_size;

    // Mapping the image file to the memory
    imageMap = mmap(
        0,              // let the kernel provide the address
        imageFileSize,  // map the entire image file
        PROT_READ,      // read only access
        MAP_PRIVATE,    // private mapping only for us
        imageFileFD,    // map this file
        0);             // from the very beginning (zero offset)
        
    if (!imageMap) {
        // Something goes wrong
        close(imageFileFD);
        imageFileFD = -1;
        return false;
    }
    
    // Initializing pointers
    imagePointer = (uint8_t*) imageMap;
    return true;
}

void Image::closeImageFile()
{
    munmap(imageMap);
    close(imageFileFD);
    
    imagePointer = 0;
    imageMap = 0;
    imageFileSize = 0;
}

uint32_t Image::readWord()
{
    if (imagePointer == (imageMap + imageFileSize) )
        return 0; // Unexpected EOF TODO break
    
    uint32_t value = 0;
    uint8_t  byte  = 0;
    
    // Very stupid yet simple multibyte encoding :-\
    // value = 255 + 255 + ... + x where x < 255
    while ( (byte = *imagePointer++) == 255 ) {
        value += byte; // adding 255 part
    }
    value += byte; // adding remaining part
    
    return value;
}

TObject* Image::readObject()
{
    // TODO error checking 
    
    TImageRecordType type = (TImageRecordType) readWord();
    switch (type) {
        case TImageRecordType::invalidObject: return 0; break;
        case TImageRecordType::ordinaryObject: {
            uint32_t fieldsCount = readWord();
            TObject* newObject = 0; // TODO static_allocate(fieldsCount); 
            indirects.push_back(newObject);
            
            TClass* objectClass = readObject(); 
            newObject->setClass(objectClass);
            
            for (int i = 0; i < fieldsCount; i++)
                newObject[i] = readObject();
            
            return newObject;
        }
        
        case TImageRecordType::inlineInteger: {
            uint32_t value = * reinterpret_cast<uint32_t*>(imagePointer);
            imagePointer += 4;
            TObject* newObject = newInteger(ntohs(value));
            return newObject;
        }
        
        case TImageRecordType::byteObject: {
            uint32_t dataSize = readWord();
            TByteObject* newByteObject = 0; // TODO static_allocate(dataSize)
            indirects.push_back(newByteObject);
            for (int i = 0; i < dataSize; i++)
                newByteObject[i] = (uint8_t) readWord();
            TClass* objectClass = readObject();
            newByteObject->setClass(objectClass);
            return newByteObject;
        }
        
        case TImageRecordType::previousObject: {
            uint32_t index = readWord();
            TObject* newObject = indirects[index];
            return newObject;
        }
        
        case TImageRecordType::nilObject:
            return indirects[0];
        
        default:
            return 0; // TODO report error
    }
}

bool Image::loadImage(const char* fileName)
{
    if (!openImageFile())
        return false;
    
    indirects.reserve(4096);
    
    globals.nilObject     = readObject();
    globals.trueObject    = readObject();
    globals.falseObject   = readObject();
    globals.globalsObject = readObject();
    globals.smallIntClass = readObject();
    globals.integerClass  = readObject();
    globals.arrayClass    = readObject();
    globals.blockClass    = readObject();
    globals.contextClass  = readObject();
    globals.stringClass   = readObject();
    globals.initialMethod = readObject();
    
    for (int i = 0; i < 3; i++)
        globals.binaryMessages[i] = readObject();
    
    globals.badMethodSymbol = readObject();
    
    indirects.clear();
    
    closeImageFile();
    
    return true;
}
