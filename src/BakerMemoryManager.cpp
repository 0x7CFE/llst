/*
 *    BakerMemoryManager.cpp
 *
 *    Implementation of BakerMemoryManager class
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.2
 *
 *    LLST is
 *        Copyright (C) 2012-2013 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2013 by Roman Proskuryakov <humbug@deeptown.org>
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
#include <cstring>
#include <cstdlib>
#include <sys/time.h>

BakerMemoryManager::BakerMemoryManager() :
    m_collectionsCount(0), m_allocationsCount(0), m_totalCollectionDelay(0),
    m_heapSize(0), m_maxHeapSize(0), m_heapOne(0), m_heapTwo(0),
    m_activeHeapOne(true), m_inactiveHeapBase(0), m_inactiveHeapPointer(0),
    m_activeHeapBase(0), m_activeHeapPointer(0), m_staticHeapSize(0),
    m_staticHeapBase(0), m_staticHeapPointer(0), m_externalPointersHead(0)
{
    // TODO set everything in m_memoryInfo to 0
    gettimeofday(&(m_memoryInfo.timeBegin), NULL);
    m_logFile.open("gc.log", std::fstream::out);
    // Nothing to be done here
}

BakerMemoryManager::~BakerMemoryManager()
{
    // TODO Reset the external pointers to catch the null pointers if something goes wrong
    m_logFile.close();
    std::free(m_staticHeapBase);
    std::free(m_heapOne);
    std::free(m_heapTwo);
}

//void BakerMemoryManager::writeLogEnd(TMemoryManagerEvent *event){
//
//}

void BakerMemoryManager::writeLogLine(TMemoryManagerEvent *event){
    m_logFile << event->time.tv_sec << "." << event->time.tv_usec 
              << ": [" << event->eventName << ": ";
    if(event->heapInfo != NULL){
        TMemoryManagerHeapInfo *eh = event->heapInfo;
        m_logFile << eh->usedHeapSizeBeforeCollect << "K -> "
                  << eh->usedHeapSizeAfterCollect << "K("
                  << eh->totalHeapSize << "K) ";
        for(std::list<TMemoryManagerHeapEvent*>::iterator i = eh->heapEvents.begin(); i != eh->heapEvents.end(); i++){
            m_logFile << "[" << (*i)->eventName << ": "
                      << (*i)->usedHeapSizeBeforeCollect << "K -> "
                      << (*i)->usedHeapSizeAfterCollect << "K("
                      << (*i)->totalHeapSize << "K) ";
            if((*i)->timeDiff.tv_sec != 0 || (*i)->timeDiff.tv_usec != 0){
                m_logFile << ", " << (*i)->timeDiff.tv_sec << "." << (*i)->timeDiff.tv_usec << " secs";
            }
            m_logFile << "] ";
        }
    }
    if(event->timeDiff.tv_sec != 0 || event->timeDiff.tv_usec != 0){
        m_logFile << ", " << event->timeDiff.tv_sec << "." << event->timeDiff.tv_usec << " secs";
    }
    m_logFile << "]\n";
}

bool BakerMemoryManager::initializeStaticHeap(std::size_t heapSize)
{
    heapSize = correctPadding(heapSize);

    uint8_t* heap = static_cast<uint8_t*>( std::malloc(heapSize) );
    if (!heap)
        return false;

    std::memset(heap, 0, heapSize);

    m_staticHeapBase = heap;
    m_staticHeapPointer = heap + heapSize;
    m_staticHeapSize = heapSize;

    return true;
}

bool BakerMemoryManager::initializeHeap(std::size_t heapSize, std::size_t maxHeapSize /* = 0 */)
{
    // To initialize properly we need a heap with an even size
    heapSize = correctPadding(heapSize);

    uint32_t mediane = heapSize / 2;
    m_heapSize = heapSize;
    m_maxHeapSize = maxHeapSize;

    m_heapOne = static_cast<uint8_t*>( std::malloc(mediane) );
    m_heapTwo = static_cast<uint8_t*>( std::malloc(mediane) );
    // TODO check for allocation errors

    std::memset(m_heapOne, 0, mediane);
    std::memset(m_heapTwo, 0, mediane);

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

    std::printf("MM: Growing heap to %d\n", newHeapSize);

    uint32_t newMediane = newHeapSize / 2;
    uint8_t** activeHeapBase   = m_activeHeapOne ? &m_heapOne : &m_heapTwo;
    uint8_t** inactiveHeapBase = m_activeHeapOne ? &m_heapTwo : &m_heapOne;

    // Reallocating space and zeroing it
    {
        void* newInactiveHeap = std::realloc(*inactiveHeapBase, newMediane);
        if (!newInactiveHeap)
        {
            std::printf("MM: Cannot reallocate %d bytes for inactive heap\n", newMediane);
            std::abort();
        } else {
            *inactiveHeapBase = static_cast<uint8_t*>(newInactiveHeap);
            std::memset(*inactiveHeapBase, 0, newMediane);
        }
    }
    // Stage 2. Collecting garbage so that
    // active objects will be moved to a new home
    collectGarbage();

    // Now pointers are swapped and previously active heap is now inactive
    // We need to reallocate it too
    {
        void* newActiveHeap = std::realloc(*activeHeapBase, newMediane);
        if (!newActiveHeap)
        {
            std::printf("MM: Cannot reallocate %d bytes for active heap\n", newMediane);
            std::abort();
        } else {
            *activeHeapBase = static_cast<uint8_t*>(newActiveHeap);
            std::memset(*activeHeapBase, 0, newMediane);
        }
    }
    collectGarbage();

    m_heapSize = newHeapSize;
}

void* BakerMemoryManager::allocate(std::size_t requestedSize, bool* gcOccured /*= 0*/ )
{
    if (gcOccured)
        *gcOccured = false;

    std::size_t attempts = 2;
    while (attempts-- > 0) {
        if (m_activeHeapPointer - requestedSize < m_activeHeapBase) {
            collectGarbage();

            // If even after collection there is too less space
            // we may try to expand the heap
            const uintptr_t distance = m_activeHeapPointer - m_activeHeapBase;
            if ((m_heapSize < m_maxHeapSize) && (distance < m_heapSize / 6))
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

    std::fprintf(stderr, "Could not allocate %u bytes in heap\n", requestedSize);
    return 0;
}

void* BakerMemoryManager::staticAllocate(std::size_t requestedSize)
{
    uint8_t* newPointer = m_staticHeapPointer - requestedSize;
    if (newPointer < m_staticHeapBase)
    {
        std::fprintf(stderr, "Could not allocate %u bytes in static heaps\n", requestedSize);
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
            if (isSmallInteger( reinterpret_cast<TObject*>(currentObject) )) {
                // Inline integers are stored directly in the
                // pointer space. All we need to do is just copy
                // contents of the poiner to a new place

                replacement   = currentObject;
                currentObject = previousObject;
                break;
            }

            bool inOldSpace = (reinterpret_cast<uint8_t*>(currentObject) >= m_inactiveHeapPointer) &&
                              (reinterpret_cast<uint8_t*>(currentObject) < (m_inactiveHeapBase + m_heapSize / 2));

            // Checking if object is not in the old space
            if (!inOldSpace)
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
                std::memcpy(destination, source, dataSize);

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
    //get statistic before collect
    m_collectionsCount++;
    TMemoryManagerEvent* event = new TMemoryManagerEvent();
    event->eventName = "GC";
    gettimeofday(&(event->time), NULL);
    long tempTimeDiff = (event->time.tv_sec - m_memoryInfo.timeBegin.tv_sec)*1000000
                      + event->time.tv_usec - m_memoryInfo.timeBegin.tv_usec;
    event->time.tv_sec = tempTimeDiff / 1000000;
    event->time.tv_usec = tempTimeDiff - event->time.tv_sec * 1000000;
    event->heapInfo = new TMemoryManagerHeapInfo;
    event->heapInfo->usedHeapSizeBeforeCollect =  (m_heapSize - (m_activeHeapPointer - m_activeHeapBase))/1024;
    event->heapInfo->totalHeapSize = m_heapSize/1024;
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

    std::memset(m_inactiveHeapBase, 0, m_heapSize / 2);

    // Calculating total microseconds spent in the garbage collection procedure
    m_totalCollectionDelay += (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec);
    event->heapInfo->usedHeapSizeAfterCollect =  (m_heapSize - (m_activeHeapPointer - m_activeHeapBase))/1024;
    event->timeDiff.tv_sec = m_totalCollectionDelay/1000000;
    event->timeDiff.tv_usec = m_totalCollectionDelay - event->timeDiff.tv_sec*1000000;
    m_memoryInfo.events.push_front(event);
    writeLogLine(event);
}

void BakerMemoryManager::moveObjects()
{
    // Here we need to check the rootStack, staticRoots and the VM execution context
    TStaticRootsIterator iRoot = m_staticRoots.begin();
    for (; iRoot != m_staticRoots.end(); ++iRoot) {
        **iRoot = moveObject(**iRoot);
    }

    // Updating external references. Typically these are pointers stored in the hptr<>
    object_ptr* currentPointer = m_externalPointersHead;
    while (currentPointer != 0) {
        currentPointer->data = reinterpret_cast<TObject*>( moveObject( reinterpret_cast<TMovableObject*>(currentPointer->data) ) );
        currentPointer = currentPointer->next;
    }
}

bool BakerMemoryManager::isInStaticHeap(void* location)
{
    return (location >= m_staticHeapPointer) && (location < m_staticHeapBase + m_staticHeapSize);
}

bool BakerMemoryManager::checkRoot(TObject* value, TObject** objectSlot)
{
    // Here we need to perform some actions depending on whether the object slot and
    // the value resides. Generally, all pointers from the static heap to the dynamic one
    // should be tracked by the GC because it may be the only valid link to the object.
    // Object may be collected otherwise.

    bool slotIsStatic  = isInStaticHeap(objectSlot);

    // Only static slots are subject of our interest
    if (slotIsStatic) {
        TObject* oldValue  = *objectSlot;

        bool valueIsStatic = isInStaticHeap(value);
        bool oldValueIsStatic = isInStaticHeap(oldValue);

        if (!valueIsStatic) {
            // Adding dynamic value to a static slot. If slot previously contained
            // the dynamic value then it means that slot was already registered before.
            // In that case we do not need to register it again.

            if (oldValueIsStatic) {
                addStaticRoot(objectSlot);
                return true; // Root list was altered
            }
        } else {
            // Adding static value to a static slot. Typically it means assigning something
            // like nilObject. We need to check what pointer was in the slot before (oldValue).
            // If it was dynamic, we need to remove the slot from the root list, so GC will not
            // try to collect a static value from the static heap (it's just a waste of time).

            if (!oldValueIsStatic) {
                removeStaticRoot(objectSlot);
                return true; // Root list was altered
            }
        }
    }

    // Root list was not altered
    return false;
}

void BakerMemoryManager::addStaticRoot(TObject** pointer)
{
    m_staticRoots.push_front( reinterpret_cast<TMovableObject**>(pointer) );
}

void BakerMemoryManager::removeStaticRoot(TObject** pointer)
{
    TStaticRootsIterator iRoot = m_staticRoots.begin();
    for (; iRoot != m_staticRoots.end(); ++iRoot) {
        if (*iRoot == reinterpret_cast<TMovableObject**>(pointer)) {
            m_staticRoots.erase(iRoot);
            return;
        }
    }
}

void BakerMemoryManager::registerExternalHeapPointer(object_ptr& pointer) {
    pointer.next = m_externalPointersHead;
    m_externalPointersHead = &pointer;
}

void BakerMemoryManager::releaseExternalHeapPointer(object_ptr& pointer) {
    if (m_externalPointersHead == &pointer) {
        m_externalPointersHead = pointer.next;
        return;
    }

    // If it is not the last element of the list
    //  we replace the given pointer with the next one
    if (pointer.next) {
        object_ptr* next_object = pointer.next;
        pointer.data = next_object->data;
        pointer.next = next_object->next;
    } else {
        // This is the last element, we have to find the previous
        // element in the list and unlink the given pointer
        object_ptr* previousPointer = m_externalPointersHead;
        while (previousPointer->next != &pointer)
            previousPointer = previousPointer->next;

        previousPointer->next = 0;
        return;
    }
}

TMemoryManagerInfo BakerMemoryManager::getStat()
{
    //FIXME collect statistic
    m_memoryInfo.leftToRightCollections = 0;
    m_memoryInfo.rightToLeftCollections = 0;
    m_memoryInfo.rightCollectionDelay   = 0;
    m_memoryInfo.allocationsCount       = m_allocationsCount;
    m_memoryInfo.collectionsCount       = m_collectionsCount;
    m_memoryInfo.totalCollectionDelay   = m_totalCollectionDelay;
    return m_memoryInfo;
}

