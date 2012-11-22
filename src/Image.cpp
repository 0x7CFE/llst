#include <vm.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <netinet/in.h>

#include <errno.h>
#include <stdlib.h>
#include <string>

TObject* Image::getGlobal(const char* name)
{
    TDictionary* globalsDictionary = globals.globalsObject;
    TObject* result = globalsDictionary->find(name);
    return result;
}

bool Image::openImageFile(const char* fileName)
{
    fprintf(stderr, "Opening file for reading: %s\n", fileName);
    
    // Opening file for reading 
    imageFileFD = open(fileName, O_RDONLY);
    if (imageFileFD < 0)
    {
        fprintf(stderr, "Failed to open file %s : %s\n", fileName, strerror(errno));
        return false;
    }
    
    // Reading file size in bytes
    struct stat st;
    if (fstat(imageFileFD, &st) < 0) {
        close(imageFileFD);
        imageFileFD = -1;
        fprintf(stderr, "Failed to get file size : %s\n", strerror(errno));
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
        fprintf(stderr, "Failed to mmap image file: %s\n", strerror(errno));
        
        // Something goes wrong
        close(imageFileFD);
        imageFileFD = -1;
        return false;
    }
    
    // Initializing pointers
    imagePointer = (uint8_t*) imageMap;
    
    fprintf(stderr, "Image file was mmaped successfully!\n");
    return true;
}

void Image::closeImageFile()
{
    munmap(imageMap, imageFileSize);
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
    
    //fprintf(stderr, "Reading image record type\n");
    TImageRecordType type = (TImageRecordType) readWord();
    //fprintf(stderr, "Reading record %d\n", (uint32_t) type);
    switch (type) {
        case invalidObject: 
            fprintf(stderr, "Invalid object at offset %p\n", (uint32_t) imagePointer - (uint32_t) imageMap);
            exit(1); 
            break;
        
        case ordinaryObject: {
            uint32_t fieldsCount = readWord();
            fprintf(stderr, "Reading ordinaryObject with %d fields \n", fieldsCount);
            
            // TODO allocate statically
            void* slot = malloc(sizeof(TObject) + fieldsCount*4);
            TObject* newObject = new(slot) TObject(fieldsCount, 0);
            indirects.push_back(newObject);
            fprintf(stderr, "Allocated object %p indirect index %d\n", (uint32_t) newObject, indirects.size()-1);
            
            TClass* objectClass  = (TClass*) readObject(); 
            newObject->setClass(objectClass);
            fprintf(stderr, "Object class is %p \n", (uint32_t) objectClass);
            
            for (int i = 0; i < fieldsCount; i++)
                newObject->putField(i, readObject());
            
            return newObject;
        }
        
        case inlineInteger: {
            uint32_t value = * reinterpret_cast<uint32_t*>(imagePointer);
            fprintf(stderr, "Reading inline integer value %d\n", value);
            imagePointer += 4;
            TInteger newObject = newInteger(ntohs(value));
            return reinterpret_cast<TObject*>(newObject);
        }
        
        case byteObject: {
            uint32_t dataSize = readWord();
            fprintf(stderr, "Reading byte object of size %d\n", dataSize);
            
            // TODO allocate statically
            void* slot = malloc(sizeof(TByteObject) + dataSize);
            TByteObject* newByteObject = new(slot) TByteObject(dataSize, 0); 
            indirects.push_back(newByteObject);
            fprintf(stderr, "Allocated byte object %p indirect index %d\n", (uint32_t) newByteObject, indirects.size()-1);
            
            for (int i = 0; i < dataSize; i++)
                newByteObject->putByte(i, (uint8_t) readWord());
            std::string bytes((const char*)newByteObject->getBytes(), dataSize);
            fprintf(stderr, "Byte object content: '%s'\n", bytes.c_str());
            
            TClass* objectClass = (TClass*) readObject();
            newByteObject->setClass(objectClass);
            fprintf(stderr, "object %p has class %p\n", (uint32_t) newByteObject, (uint32_t) objectClass);
            
            return newByteObject;
        }
        
        case previousObject: {
            uint32_t index = readWord();
            TObject* newObject = indirects[index];
            fprintf(stderr, "Reading link to previousObject index %d address %p\n", index, (uint32_t) newObject);
            return newObject;
        }
        
        case nilObject:
            fprintf(stderr, "Reading nilObject address %p\n", (uint32_t) indirects[0]);
            return indirects[0]; // nilObject is always the first in the image
        
        default:
            fprintf(stderr, "Unknown record type %d\n", type);
            exit(1); // TODO report error
    }
}

bool Image::loadImage(const char* fileName)
{
    fprintf(stderr, "Trying to open image file: %s\n", fileName);
    
    if (!openImageFile(fileName))
    {
        fprintf(stderr, "could not open image file %s\n", fileName);
        return false;
    }
    
    fprintf(stderr, "Reserving memory for indirects\n");
    
    indirects.reserve(4096);
    
    globals.nilObject     = readObject();
    
    globals.trueObject    = readObject();
    globals.falseObject   = readObject();
    globals.globalsObject = (TDictionary*) readObject();
    globals.smallIntClass = (TClass*)  readObject();
    globals.integerClass  = (TClass*)  readObject();
    globals.arrayClass    = (TClass*)  readObject();
    globals.blockClass    = (TClass*)  readObject();
    globals.contextClass  = (TClass*)  readObject();
    globals.stringClass   = (TClass*)  readObject();
    globals.initialMethod = (TMethod*) readObject();
    
    for (int i = 0; i < 3; i++)
        globals.binaryMessages[i] = readObject();
    
    globals.badMethodSymbol = readObject();
    
    fprintf(stderr, "Image read complete. Loaded %d objects\n", indirects.size());
    indirects.clear();
    
    closeImageFile();
    
    return true;
}
