/*
 *    BakerMemoryManager.cpp 
 *    
 *    Implementation of BakerMemoryManager class
 *    
 *    LLST (LLVM Smalltalk or Lo Level Smalltalk) version 0.1
 *    
 *    LLST is 
 *        Copyright (C) 2012 by Dmitry Kashitsyn   aka Korvin aka Halt <korvin@deeptown.org>
 *        Copyright (C) 2012 by Roman Proskuryakov aka Humbug          <humbug@deeptown.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

BakerMemoryManager::BakerMemoryManager() :
    m_collectionsCount(0), m_allocationsCount(0), m_totalCollectionDelay(0), 
    m_heapSize(0), m_maxHeapSize(0), m_heapOne(0), m_heapTwo(0),
    m_activeHeapOne(true), m_inactiveHeapBase(0), m_inactiveHeapPointer(0),
    m_activeHeapBase(0), m_activeHeapPointer(0), m_staticHeapSize(0),
    m_staticHeapBase(0), m_staticHeapPointer(0)
{
    // Nothing to be done here
}

BakerMemoryManager::~BakerMemoryManager()
{
    // TODO Reset the external pointers to catch the null pointers if something goes wrong
    free(m_staticHeapBase);
    free(m_heapOne);
}

bool BakerMemoryManager::initializeStaticHeap(size_t heapSize)
{
    heapSize = correctPadding(heapSize);

    void* heap = malloc(heapSize);
    if (!heap)
        return false;

    memset(heap, 0, heapSize);

    m_staticHeapBase = (uint8_t*) heap;
    m_staticHeapPointer = (uint8_t*) heap + heapSize;
    m_staticHeapSize = heapSize;

    return true;
}

bool BakerMemoryManager::initializeHeap(size_t heapSize, size_t maxHeapSize /* = 0 */)
{
    // To initialize properly we need a heap with an even size
    heapSize = correctPadding(heapSize);

    uint32_t mediane = heapSize / 2;
    m_heapSize = heapSize;
    m_maxHeapSize = maxHeapSize;

    m_heapOne = (uint8_t*) malloc(mediane);
    m_heapTwo = (uint8_t*) malloc(mediane);
    // TODO check for allocation errors

    memset(m_heapOne, 0, mediane);
    memset(m_heapTwo, 0, mediane);
    
    m_activeHeapOne = true;
    
    m_activeHeapBase = m_heapOne;
    m_activeHeapPointer = m_heapOne + mediane;
    
    m_inactiveHeapBase =  m_heapTwo;
    m_inactiveHeapPointer = m_heapTwo + mediane;

    return true;
}

void BakerMemoryManager::growHeap(uint32_t requestedSize)
{
    // Stage1. Growing inactive heap
    uint32_t newHeapSize = correctPadding(requestedSize + m_heapSize + m_heapSize / 2);

    printf("MM: Growing heap to %d\n", newHeapSize);
    
    uint32_t newMediane = newHeapSize / 2;
    uint8_t** activeHeapBase   = m_activeHeapOne ? &m_heapOne : &m_heapTwo;
    uint8_t** inactiveHeapBase = m_activeHeapOne ? &m_heapTwo : &m_heapOne;
    
    // Reallocating space and zeroing it
    *inactiveHeapBase = (uint8_t*) realloc(*inactiveHeapBase, newMediane);
    memset(*inactiveHeapBase, 0, newMediane);

    // Stage 2. Collecting garbage so that 
    // active objects will be moved to a new home
    collectGarbage();

    // Now pointers are swapped and previously active heap is now inactive
    // We need to reallocate it too
    *activeHeapBase = (uint8_t*) realloc(*activeHeapBase, newMediane);
    memset(*activeHeapBase, 0, newMediane);
    
    m_heapSize = newHeapSize;
}

void* BakerMemoryManager::allocate(size_t requestedSize, bool* gcOccured /*= 0*/ )
{
    if (gcOccured)
        *gcOccured = false;

    size_t attempts = 2;
    while (attempts-- > 0) {
        if (m_activeHeapPointer - requestedSize < m_activeHeapBase) {
            collectGarbage();

            // If even after collection there is too less space
            // we may try to expand the heap
            if ((m_heapSize < m_maxHeapSize) && (m_activeHeapPointer - m_activeHeapBase < m_heapSize / 8))
                growHeap(requestedSize);

            if (gcOccured)
                *gcOccured = true;
            continue;
        }

        m_activeHeapPointer -= requestedSize;
        void* result = m_activeHeapPointer;

        if (gcOccured && !*gcOccured)
            m_allocationsCount++;
        return result;
    }

    // TODO Grow the heap if object still not fits

    fprintf(stderr, "Could not allocate %d bytes in heap\n", requestedSize);
    return 0;
}

void* BakerMemoryManager::staticAllocate(size_t requestedSize)
{
    uint8_t* newPointer = m_staticHeapPointer - requestedSize;
    if (newPointer < m_staticHeapBase)
    {
        fprintf(stderr, "Could not allocate %d bytes in static heaps\n", requestedSize);
        return 0; // TODO Report memory allocation error
    }
    m_staticHeapPointer = newPointer;
    return newPointer;
}

BakerMemoryManager::TMovableObject* BakerMemoryManager::moveObject(TMovableObject* object)
{
    TMovableObject* currentObject  = object;
    TMovableObject* previousObject = 0;
    TMovableObject* objectCopy     = 0;
    TMovableObject* replacement    = 0;

    while (true) {

        // Stage 1. Walking down the tree. Keep stacking objects to be moved
        // until we find one that we can handle
        while (true) {
            // Checking whether this is inline integer
            if (isSmallInteger((TObject*) currentObject)) {
                // Inline integers are stored directly in the
                // pointer space. All we need to do is just copy
                // contents of the poiner to a new place

                replacement   = currentObject;
                currentObject = previousObject;
                break;
            }

            // Checking if object is not in the old space
            if ( (currentObject > (TMovableObject*) (m_inactiveHeapBase + m_heapSize / 2))
              || (currentObject < (TMovableObject*) m_inactiveHeapPointer))
            {
                // Object does not belong to a heap.
                // Either it is located in static space 
                // or this is a broken pointer
                replacement   = currentObject;
                currentObject = previousObject;
                break;
            }

            // Checking if object is already moved
            if (currentObject->size.isRelocated()) {
                if (currentObject->size.isBinary()) {
                    replacement = currentObject->data[0];
                } else {
                    uint32_t index = currentObject->size.getSize();
                    replacement = currentObject->data[index];
                }
                currentObject = previousObject;
                break;
            }

            // Checking whether we're dealing with the binary object
            if (currentObject->size.isBinary()) {
                // Current object is binary.
                // Moving object to a new space, copying it's data
                // and finally walking up to the object's class

                // Size of binary data
                uint32_t dataSize = currentObject->size.getSize();

                // Allocating copy in new space

                // We need to allocate space evenly, so calculating the
                // actual size of the block being reserved for the moving object
                m_activeHeapPointer -= sizeof(TByteObject) + correctPadding(dataSize); 
                objectCopy = new (m_activeHeapPointer) TMovableObject(dataSize, true);

                // Copying byte data. data[0] is the class pointer, 
                // actual binary data starts from the data[1]
                uint8_t* source      = reinterpret_cast<uint8_t*>( & currentObject->data[1] );
                uint8_t* destination = reinterpret_cast<uint8_t*>( & objectCopy->data[1] );
                memcpy(destination, source, dataSize);

                // Marking original copy of object as relocated so it would not be processed again
                currentObject->size.setRelocated();

                // During GC process temporarily using data[0] as indirection pointer
                // This will be corrected on the next stage of current GC operation
                objectCopy->data[0] = previousObject;
                previousObject = currentObject;
                currentObject  = currentObject->data[0];
                previousObject->data[0] = objectCopy;

                // On the next iteration we'll be processing the data[0] of the current object
                // which is actually class pointer in TObject.
                // NOTE It is expected that class of binary object would be non binary
            } else {
                // Current object is not binary, i.e. this is an ordinary object
                // with fields that are either SmallIntegers or pointers to other objects

                uint32_t fieldsCount = currentObject->size.getSize();

                m_activeHeapPointer -= sizeof(TObject) + fieldsCount * sizeof (TObject*);
                objectCopy = new (m_activeHeapPointer) TMovableObject(fieldsCount, false);

                currentObject->size.setRelocated();

                // Initializing indices. Actual field copying
                // will be done later in the next subloop
                const uint32_t lastObjectIndex = fieldsCount;
                objectCopy->data[lastObjectIndex] = previousObject;
                previousObject = currentObject;
                currentObject  = currentObject->data[lastObjectIndex];
                previousObject->data[lastObjectIndex] = objectCopy;
            }
        }

        // Stage 2.  Fix up pointers,
        // Move back up tree as long as possible
        // old_address points to an object in the old space,
        // which in turns points to an object in the new space,
        // which holds a pointer that is now to be replaced.
        // the value in replacement is the new value

        while (true) {
            // We're got out entirely
            if (currentObject == 0)
                return replacement;

            // Either binary object or the last value (field from the ordinary one?)
            if ( currentObject->size.isBinary() || (currentObject->size.getSize() == 0) ) {
                // Fixing up class pointer
                objectCopy = currentObject->data[0];

                previousObject = objectCopy->data[0];
                objectCopy->data[0] = replacement;
                currentObject->data[0] = objectCopy;

                replacement = objectCopy;
                currentObject = previousObject;
            } else {
                // last field from TObject
                uint32_t lastFieldIndex = currentObject->size.getSize();

                objectCopy = currentObject->data[lastFieldIndex];
                previousObject = objectCopy->data[lastFieldIndex];
                objectCopy->data[lastFieldIndex] = replacement;

                // Recovering zero fields
                lastFieldIndex--;
                while((lastFieldIndex > 0) && (currentObject->data[lastFieldIndex] == 0))
                {
                    objectCopy->data[lastFieldIndex] = 0;
                    lastFieldIndex--;
                }

                // Storing the last visited index to the size
                // If it gets zero then all fields were moved
                currentObject->size.setSize(lastFieldIndex);
                currentObject->size.setRelocated();

                objectCopy->data[lastFieldIndex] = previousObject;
                previousObject = currentObject;
                currentObject = currentObject->data[lastFieldIndex];
                previousObject->data[lastFieldIndex] = objectCopy;
                break;
            }
        }
    }
}

void BakerMemoryManager::collectGarbage()
{
    m_collectionsCount++;

    // First of all swapping the spaces
    if (m_activeHeapOne)
    {
        m_activeHeapBase = m_heapTwo;
        m_inactiveHeapBase = m_heapOne;
    } else {
        m_activeHeapBase = m_heapOne;
        m_inactiveHeapBase = m_heapTwo;
    }

    m_activeHeapOne = not m_activeHeapOne;

    m_inactiveHeapPointer = m_activeHeapPointer;
    m_activeHeapPointer = m_activeHeapBase + m_heapSize / 2;

    // Then, performing the collection. Seeking from the root
    // objects down the hierarchy to find active objects.
    // Then moving them to the new active heap.

    // Storing timestamp on start
    timeval tv1;
    gettimeofday(&tv1, NULL);

    // Moving the live objects in the new heap
    moveObjects();
    
    // Storing timestamp of the end
    timeval tv2;
    gettimeofday(&tv2, NULL);
    
    // Calculating total microseconds spent in the garbage collection procedure
    m_totalCollectionDelay += (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec);
}

void BakerMemoryManager::moveObjects()
{
    // Here we need to check the rootStack, staticRoots and the VM execution context
    TStaticRootsIterator iRoot = m_staticRoots.begin();
    for (; iRoot != m_staticRoots.end(); ++iRoot) {
        **iRoot = moveObject(**iRoot);
    }
    
    // Updating external references. Typically this is pointers stored in the hptr<>
    TPointerIterator iExternalPointer = m_externalPointers.begin();
    for (; iExternalPointer != m_externalPointers.end(); ++iExternalPointer) {
        **iExternalPointer = moveObject(**iExternalPointer);
    }
}

bool BakerMemoryManager::isInStaticHeap(void* location)
{
    return (location >= m_staticHeapPointer) && (location < m_staticHeapBase + m_staticHeapSize);
}

void BakerMemoryManager::addStaticRoot(TObject** pointer)
{
    m_staticRoots.push_front((TMovableObject**) pointer);
}

void BakerMemoryManager::removeStaticRoot(TObject** pointer)
{
    TStaticRootsIterator iRoot = m_staticRoots.begin();
    for (; iRoot != m_staticRoots.end(); ++iRoot) {
        if (*iRoot == (TMovableObject**) pointer) {
            m_staticRoots.erase(iRoot);
            return;
        }
    }
}

void BakerMemoryManager::registerExternalPointer(TObject** pointer)
{
    m_externalPointers.push_front((TMovableObject**) pointer);
}

void BakerMemoryManager::releaseExternalPointer(TObject** pointer)
{
    TPointerIterator iPointer = m_externalPointers.begin();
    for (; iPointer != m_externalPointers.end(); ++iPointer) {
        if (*iPointer == (TMovableObject**) pointer) {
            m_externalPointers.erase(iPointer);
            return;
        }
    }
}

TMemoryManagerInfo BakerMemoryManager::getStat() 
{
    TMemoryManagerInfo info;
    info.allocationsCount     = m_allocationsCount;
    info.collectionsCount     = m_collectionsCount;
    info.totalCollectionDelay = m_totalCollectionDelay;
    return info;
}