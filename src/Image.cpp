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

uint32_t Image::readWord()
{
    if (imagePointer == (imageMap + imageFileSize) )
        return 0; // Unexpected EOF TODO break
    
    uint32_t value = 0;
    uint8_t  byte  = 0;
    
    // value = 255 + 255 + ... + x where x < 255
    while ( (byte = *imagePointer++) == 255 ) {
        value += byte; // adding 255 part
    }
    value += byte; // adding remaining part
    
    return value;
}

bool Image::loadImage(const char* fileName)
{
    if (!openImageFile())
        return false;
    
    
}
