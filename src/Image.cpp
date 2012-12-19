#include <vm.h>
#include <memory.h>

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

// Placeholder for root objects
TGlobals globals;


TObject* Image::getGlobal(const char* name)
{
    TDictionary* globalsDictionary = globals.globalsObject;
    TObject* result = globalsDictionary->find(name);
    return result;
}

TObject* Image::getGlobal(TSymbol* name)
{
    TDictionary* globalsDictionary = globals.globalsObject;
    TObject* result = globalsDictionary->find(name);
    return result;
}

bool Image::openImageFile(const char* fileName)
{
    // Opening file for reading 
    m_imageFileFD = open(fileName, O_RDONLY);
    if (m_imageFileFD < 0)
    {
        fprintf(stderr, "Failed to open file %s : %s\n", fileName, strerror(errno));
        return false;
    }
    
    // Reading file size in bytes
    struct stat st;
    if (fstat(m_imageFileFD, &st) < 0) {
        close(m_imageFileFD);
        m_imageFileFD = -1;
        fprintf(stderr, "Failed to get file size : %s\n", strerror(errno));
        return false;
    }
    m_imageFileSize = st.st_size;

    // Mapping the image file to the memory
    m_imageMap = mmap(
        0,              // let the kernel provide the address
        m_imageFileSize,  // map the entire image file
        PROT_READ,      // read only access
        MAP_PRIVATE,    // private mapping only for us
        m_imageFileFD,    // map this file
        0);             // from the very beginning (zero offset)
        
    if (!m_imageMap) {
        fprintf(stderr, "Failed to mmap image file: %s\n", strerror(errno));
        
        // Something goes wrong
        close(m_imageFileFD);
        m_imageFileFD = -1;
        return false;
    }
    
    // Initializing pointers
    m_imagePointer = (uint8_t*) m_imageMap;
    return true;
}

void Image::closeImageFile()
{
    munmap(m_imageMap, m_imageFileSize);
    close(m_imageFileFD);
    
    m_imagePointer = 0;
    m_imageMap = 0;
    m_imageFileSize = 0;
}

uint32_t Image::readWord()
{
    if (m_imagePointer == ((uint8_t*)m_imageMap + m_imageFileSize) )
        return 0; // Unexpected EOF TODO break
    
    uint32_t value = 0;
    uint8_t  byte  = 0;
    
    // Very stupid yet simple multibyte encoding
    // value = 255 + 255 + ... + x where x < 255
    while ( (byte = *m_imagePointer++) == 255 ) {
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
            fprintf(stderr, "Invalid object at offset %p\n", (void*) (m_imagePointer - (uint8_t*)m_imageMap));
            exit(1); 
            break;
        
        case ordinaryObject: {
            uint32_t fieldsCount = readWord();
            
            size_t slotSize   = sizeof(TObject) + fieldsCount * sizeof(TObject*);
            void*  objectSlot = m_memoryManager->staticAllocate(slotSize);
            
            TObject* newObject = new(objectSlot) TObject(fieldsCount, 0);
            m_indirects.push_back(newObject);
            
            TClass* objectClass  = (TClass*) readObject(); 
            newObject->setClass(objectClass);
            
            for (uint32_t i = 0; i < fieldsCount; i++)
                newObject->putField(i, readObject());
                //FIXME ? globals.initialMethod.temporarySize pointing to nilObject. i == 4
            
            return newObject;
        }
        
        case inlineInteger: {
            //uint32_t value = * reinterpret_cast<uint32_t*>(m_imagePointer);
            uint32_t value = m_imagePointer[0] | (m_imagePointer[1] << 8) | 
                            (m_imagePointer[2] << 16) | (m_imagePointer[3] << 24);
            m_imagePointer += sizeof(uint32_t);
            TInteger newObject = newInteger(value); // FIXME endianness
            return reinterpret_cast<TObject*>(newObject);
        }
        
        case byteObject: {
            uint32_t dataSize = readWord();
            
            size_t slotSize   = sizeof(TByteObject) + dataSize;
            
            // We need to align memory by even addresses so that 
            // normal pointers will always have the lowest bit 0
            slotSize = (slotSize + sizeof(TObject*) - 1) & ~0x3;
            
            void*  objectSlot = m_memoryManager->staticAllocate(slotSize);
            TByteObject* newByteObject = new(objectSlot) TByteObject(dataSize, 0); 
            m_indirects.push_back(newByteObject);
            
            for (uint32_t i = 0; i < dataSize; i++)
                (*newByteObject)[i] = (uint8_t) readWord();
            
            TClass* objectClass = (TClass*) readObject();
            newByteObject->setClass(objectClass);
            
            return newByteObject;
        }
        
        case previousObject: {
            uint32_t index = readWord();
            TObject* newObject = m_indirects[index];
            return newObject;
        }
        
        case nilObject:
            return m_indirects[0]; // nilObject is always the first in the image
        
        default:
            fprintf(stderr, "Unknown record type %d\n", type);
            exit(1); // TODO report error
    }
}

bool Image::loadImage(const char* fileName)
{
    if (!openImageFile(fileName))
    {
        fprintf(stderr, "could not open image file %s\n", fileName);
        return false;
    }
    
    // TODO Check whether heap is already initialized
    
    // Multiplier of 1.5 of imageFileSize should be a good estimation for static heap size
    if (!m_memoryManager->initializeStaticHeap(m_imageFileSize + m_imageFileSize / 2) )
    {
        closeImageFile();
        return false;
    }
    
    m_indirects.reserve(4096);
    
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
    
    globals.badMethodSymbol = (TSymbol*) readObject();
    
    fprintf(stdout, "Image read complete. Loaded %d objects\n", m_indirects.size());
    m_indirects.clear();
    
    closeImageFile();
    
    return true;
}
