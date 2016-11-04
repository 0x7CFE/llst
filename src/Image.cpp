/*
 *    Image.cpp
 *
 *    Implementation of Image class which loads the file image into memory
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.4
 *
 *    LLST is
 *        Copyright (C) 2012-2015 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2015 by Roman Proskuryakov <humbug@deeptown.org>
 *
 *    LLST is based on the LittleSmalltalk which is
 *        Copyright (C) 1987-2005 by Timothy A. Budd
 *        Copyright (C) 2007 by Charles R. Childers
 *        Copyright (C) 2005-2007 by Danny Reinhold
 *
 *    Original license of LittleSmalltalk may be found in the LICENSE file.
 *
 *
 *    This file is part of LLST.
 *    LLST is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    LLST is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with LLST.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <memory.h>

//#include <netinet/in.h> //TODO endianness

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <string>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <stdexcept>

// Placeholder for root objects
TGlobals globals;

template<typename N>
TObject* Image::getGlobal(const N* name) const {
    TDictionary* globalsDictionary = globals.globalsObject;
    TObject* result = globalsDictionary->find(name);
    return result;
}

template TObject* Image::getGlobal<char>(const char* key) const;
template TObject* Image::getGlobal<TSymbol>(const TSymbol* key) const;

uint32_t Image::readWord()
{
    uint32_t value = 0;
    uint8_t  byte  = 0;

    // Very stupid yet simple multibyte encoding
    // value = FF + FF ... + x where x < FF
    do {
        byte = m_inputStream.get();
        value += byte;
    } while ( byte == 0xFF );
    return value;
}

TObject* Image::readObject()
{
    // TODO error checking

    TImageRecordType type = static_cast<TImageRecordType>(readWord());
    switch (type) {
        case invalidObject:
            std::cerr << "Invalid object at offset " << m_inputStream.tellg() << std::endl;
            std::exit(1);
            break;

        case ordinaryObject: {
            uint32_t fieldsCount = readWord();

            std::size_t slotSize = sizeof(TObject) + fieldsCount * sizeof(TObject*);
            void* objectSlot = m_memoryManager->staticAllocate(slotSize);
            TObject* newObject = new(objectSlot) TObject(fieldsCount, 0);
            m_indirects.push_back(newObject);

            TClass* objectClass  = readObject<TClass>();
            newObject->setClass(objectClass);

            for (uint32_t i = 0; i < fieldsCount; i++)
                newObject->putField(i, readObject());

            return newObject;
        }

        case inlineInteger: {
            uint32_t value = m_inputStream.get() | (m_inputStream.get() << 8) |
                            (m_inputStream.get() << 16) | (m_inputStream.get() << 24);
            return TInteger(value); // FIXME endianness
        }

        case byteObject: {
            uint32_t dataSize = readWord();

            std::size_t slotSize = sizeof(TByteObject) + dataSize;

            // We need to align memory by even addresses so that
            // normal pointers will always have the lowest bit 0
            slotSize = correctPadding(slotSize);

            void* objectSlot = m_memoryManager->staticAllocate(slotSize);
            TByteObject* newByteObject = new(objectSlot) TByteObject(dataSize, 0);
            m_indirects.push_back(newByteObject);

            for (uint32_t i = 0; i < dataSize; i++)
                (*newByteObject)[i] = static_cast<uint8_t>(readWord());

            TClass* objectClass = readObject<TClass>();
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
            std::fprintf(stderr, "Unknown record type %d\n", type);
            std::exit(1); // TODO report error
    }
}

bool Image::loadImage(const std::string& fileName)
{
    m_inputStream.exceptions( std::ifstream::eofbit | std::ifstream::badbit );
    m_inputStream.open(fileName.c_str(), std::ifstream::binary);

    if ( !m_inputStream.is_open() ) {
        throw std::runtime_error("Could not open image file " + fileName);
    }

    //Get the size of the file
    m_inputStream.seekg(0, std::ifstream::end);
    std::ifstream::pos_type fileSize = m_inputStream.tellg();

    //Reset the position to the beginning of the file
    m_inputStream.seekg(0);

    // TODO Check whether heap is already initialized
    // Multiplier of 1.5 of imageFileSize should be a good estimation for static heap size
    if ( !m_memoryManager->initializeStaticHeap(fileSize * 1.5) ) {
        return false;
    }

    m_indirects.reserve(4096);

    globals.nilObject     = readObject();

    globals.trueObject    = readObject();
    globals.falseObject   = readObject();
    globals.globalsObject = readObject<TDictionary>();
    globals.smallIntClass = readObject<TClass>();
    globals.integerClass  = readObject<TClass>();
    globals.arrayClass    = readObject<TClass>();
    globals.blockClass    = readObject<TClass>();
    globals.contextClass  = readObject<TClass>();
    globals.stringClass   = readObject<TClass>();
    globals.initialMethod = readObject<TMethod>();

    for (int i = 0; i < 3; i++)
        globals.binaryMessages[i] = readObject();

    globals.badMethodSymbol = readObject<TSymbol>();
    globals.processClass = getGlobal<TClass>("Process");

    std::fprintf(stdout, "Image read complete. Loaded %d objects\n", m_indirects.size());
    m_indirects.clear();

    return true;
}

void Image::ImageWriter::writeWord(std::ofstream& os, uint32_t word)
{
    while (word >= 0xFF) {
        word -= 0xFF;
        os << '\xFF';
    }
    uint8_t byte = word & 0xFF;
    os << byte;
}

Image::TImageRecordType Image::ImageWriter::getObjectType(TObject* object) const
{
    if ( isSmallInteger(object) ) {
        return inlineInteger;
    } else {
        std::vector<TObject*>::const_iterator iter = std::find(m_writtenObjects.begin(), m_writtenObjects.end(), object);
        if (iter != m_writtenObjects.end()) {
            // object is found
            int index = std::distance(m_writtenObjects.begin(), iter);
            if (index == 0)
                return nilObject;
            else
                return previousObject;
        }
        else if ( object->isBinary() )
            return byteObject;
        else
            return ordinaryObject;
    }
}

int Image::ImageWriter::getPreviousObjectIndex(TObject* object) const
{
    std::vector<TObject*>::const_iterator iter = std::find(m_writtenObjects.begin(), m_writtenObjects.end(), object);
    assert(iter != m_writtenObjects.end());
    return std::distance(m_writtenObjects.begin(), iter);
}

void Image::ImageWriter::writeObject(std::ofstream& os, TObject* object)
{
    assert(object != 0);
    TImageRecordType type = getObjectType(object);
    writeWord(os, static_cast<uint32_t>(type));

    if (type == ordinaryObject || type == byteObject)
        m_writtenObjects.push_back(object);

    switch (type) {
        case inlineInteger: {
            uint32_t integer = TInteger(object);
            os.write(reinterpret_cast<char*>(&integer), sizeof(integer));
        } break;
        case byteObject: {
            TByteObject* byteObject = static_cast<TByteObject*>(object);
            uint32_t fieldsCount = byteObject->getSize();
            TClass* objectClass = byteObject->getClass();
            assert(objectClass != 0);

            writeWord(os, fieldsCount);
            for (uint32_t i = 0; i < fieldsCount; i++)
                writeWord(os, byteObject->getByte(i));

            writeObject(os, objectClass);
        } break;
        case ordinaryObject: {
            uint32_t fieldsCount = object->getSize();
            TClass* objectClass = object->getClass();
            assert(objectClass != 0);

            writeWord(os, fieldsCount);
            writeObject(os, objectClass);
            for (uint32_t i = 0; i < fieldsCount; i++)
                writeObject(os, object->getField(i));
        } break;
        case previousObject: {
            int index = getPreviousObjectIndex(object);
            writeWord(os, index);
        } break;
        case nilObject: {
            // type nilObject means a link to nilObject
            // it has already been written as the first object with type ordinaryObject
        } break;
        default:
            std::fprintf(stderr, "unexpected type of object: %d\n", static_cast<int>(type));
            std::exit(1);
    }
}

Image::ImageWriter::ImageWriter() {
   std::memset(&m_globals, 0, sizeof(m_globals));
}

Image::ImageWriter& Image::ImageWriter::setGlobals(const TGlobals& globals)
{
    m_globals = globals;
    return *this;
}

void Image::ImageWriter::writeTo(const char* fileName)
{
    std::ofstream os(fileName);

    m_writtenObjects.clear();
    m_writtenObjects.reserve(8096);

    writeObject(os, m_globals.nilObject);
    writeObject(os, m_globals.trueObject);
    writeObject(os, m_globals.falseObject);
    writeObject(os, m_globals.globalsObject);
    writeObject(os, m_globals.smallIntClass);
    writeObject(os, m_globals.integerClass);
    writeObject(os, m_globals.arrayClass);
    writeObject(os, m_globals.blockClass);
    writeObject(os, m_globals.contextClass);
    writeObject(os, m_globals.stringClass);
    writeObject(os, m_globals.initialMethod);

    for (int i = 0; i < 3; i++)
        writeObject(os, m_globals.binaryMessages[i]);

    writeObject(os, m_globals.badMethodSymbol);

    m_writtenObjects.clear();
}
