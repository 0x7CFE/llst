/*
 *    primitives.cpp
 *
 *    Implementation of generational memory manager which extends
 *    original Baker memory manager by introducing asymmetrical
 *    handling of heap parts.
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

#include <cstdlib>
#include <cstring>
#include <sys/time.h>

GenerationalMemoryManager::~GenerationalMemoryManager()
{
    // Nothing to do here
}

void GenerationalMemoryManager::moveYoungObjects()
{
    TPointerIterator iYoungPointer = m_crossGenerationalReferences.begin();
    for (; iYoungPointer != m_crossGenerationalReferences.end(); ++iYoungPointer) {
        **iYoungPointer = moveObject(**iYoungPointer);
    }

    // Now all active young objects are moved to the old space.
    // The old space is collected with traditional algorithm, so
    // cross generational references are not needed anymore.
    m_crossGenerationalReferences.clear();

    // Updating external references. Typically these are pointers stored in the hptr<>
    object_ptr* currentPointer = m_externalPointersHead;
    while (currentPointer != 0) {
        TMovableObject* currentObject = reinterpret_cast<TMovableObject*>(currentPointer->data);
        uint8_t* currentObjectBase    = reinterpret_cast<uint8_t*>(currentObject);

        if ( (currentObjectBase >= m_inactiveHeapPointer ) &&
            (currentObjectBase < m_heapOne + m_heapSize / 2))
        {
            currentObject = moveObject(currentObject);
        }
        currentPointer = currentPointer->next;
    }

    TStaticRootsIterator iRoot = m_staticRoots.begin();
    for (; iRoot != m_staticRoots.end(); ++iRoot) {
        //if (isInYoungHeap(**iRoot))
        uint8_t* currentRootBase   = reinterpret_cast<uint8_t*>(*iRoot);
        uint8_t* currentObjectBase = reinterpret_cast<uint8_t*>(**iRoot);


        if ( ((currentObjectBase >= m_inactiveHeapPointer) && (currentObjectBase < (m_heapOne + m_heapSize / 2)))
            || ((currentRootBase >= m_inactiveHeapPointer) && (currentRootBase < (m_heapOne + m_heapSize / 2)))
        )
        {
            **iRoot = moveObject(**iRoot);
        }
    }

}

void GenerationalMemoryManager::collectGarbage()
{
//     printf("GMM: collectGarbage()\n");

    // Generational GC takes advantage of a fact that most objects are alive
    // for a very short amount of time. Those who survived the first collection
    // are typically stay there for much longer.
    //
    // In classic Baker collector both spaces are equal in rights and
    // are used interchangebly. In Generational GC right space is selected
    // as a storage for long living generation 1 whereas immediate generation 0
    // objects are repeatedly allocated in the space one even after collection.

    // In most frequent collection mode LeftToRight we move generation 0 objects from the
    // left heap (heap one) to the right heap (heap two) so they'll become a generation 1 objects.
    //
    // After objects are moved two possible scenarios exist:
    //
    // 1. Normally, heap one is cleared and again used for further
    // allocations.
    //
    // 2. If amount of free space in the heap two is below threshold,
    // additional collection takes place which moves all objects
    // to the left space and resets the state to csRightSpaceEmpty.

    // Storing timestamp on start
    timeval tv1;
    gettimeofday(&tv1, NULL);

    collectLeftToRight();
    if (checkThreshold())
        collectRightToLeft();

    // Storing timestamp of the end
    timeval tv2;
    gettimeofday(&tv2, NULL);

    // Calculating total microseconds spent in the garbage collection procedure
    m_memoryInfo.totalCollectionDelay += (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec);

    m_memoryInfo.collectionsCount++;
}

void GenerationalMemoryManager::collectLeftToRight(bool fullCollect /*= false*/)
{
    // Classic baker algorithm moves objects after swapping the spaces,
    // but in our case we do not want to swap them now. Still, in order to
    // satisfy moveObjects() we do this temporarily and then revert the pointers
    // to the needed state.

    // Setting the heap two as active leaving the heap pointer as is
    m_activeHeapBase    = m_heapTwo;
    m_inactiveHeapBase  = m_heapOne;

    std::swap(m_activeHeapPointer, m_inactiveHeapPointer);

    // Moving the objects from the left to the right heap
    // TODO In certain circumstances right heap may not have
    //      enough space to store all active objects from gen 0.
    //      This may happen if massive allocation took place before
    //      the collection was initiated. In this case we need to interrupt
    //      the collection, then allocate a new larger heap and recollect
    //      ALL objects from both spaces to a new allocated heap.
    //      After that old space is freed and new is treated either as heap one
    //      or heap two depending on the size.
    if (fullCollect) {
        moveObjects();
    } else {
        moveYoungObjects();
    }

    m_inactiveHeapBase    = m_heapTwo;
    m_inactiveHeapPointer = m_activeHeapPointer;

    // Now, all active objects are located in the space two
    // (inactive space in terms of classic Baker).
    // Resetting the space one pointers to mark space as empty.
    m_activeHeapBase    = m_heapOne;
    m_activeHeapPointer = m_activeHeapBase + m_heapSize / 2;

    std::memset(m_heapOne, 0xAA, m_heapSize / 2);

    // After this operation active objects from space one now all
    // in space two and are treated as generation 1.
    m_leftToRightCollections++;
}

void GenerationalMemoryManager::collectRightToLeft()
{
    // Storing timestamp on start
    timeval tv1;
    gettimeofday(&tv1, NULL);

    m_activeHeapBase    = m_heapOne;
    m_inactiveHeapBase  = m_heapTwo;

    // m_inactiveHeapPointer remains the same
    m_activeHeapPointer = m_heapOne + m_heapSize / 2;

    moveObjects();

    // Objects were moved from right heap to the left one.
    // Now right heap may be emptied by resetting the heap pointer

    // Resetting heap two
    m_inactiveHeapPointer = m_heapTwo + m_heapSize / 2;
    // m_activeHeapPointer = ?

    std::memset(m_heapTwo, 0xBB, m_heapSize / 2);

    // Moving objects back to the right heap
    collectLeftToRight(true);

    // m_activeHeapPointer remains there and used for futher allocations
    // because heap one remains active
    m_rightToLeftCollections++;

    // Storing timestamp of the end
    timeval tv2;
    gettimeofday(&tv2, NULL);

    // Calculating total microseconds spent in the garbage collection procedure
    m_rightCollectionDelay += (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec);
}

bool GenerationalMemoryManager::checkThreshold()
{
    const uintptr_t distance = m_inactiveHeapPointer - m_inactiveHeapBase;
    return (distance < m_heapSize / 8);
}

TMemoryManagerInfo GenerationalMemoryManager::getStat() {
    TMemoryManagerInfo info = BakerMemoryManager::getStat();
    info.leftToRightCollections = m_leftToRightCollections;
    info.rightToLeftCollections = m_rightToLeftCollections;
    info.rightCollectionDelay = m_rightCollectionDelay;
    return info;
}

bool GenerationalMemoryManager::isInYoungHeap(void* location)
{
    return (location >= m_activeHeapPointer) && (location < m_heapOne + m_heapSize / 2);
}

bool GenerationalMemoryManager::checkRoot(TObject* value, TObject** objectSlot)
{
    // checkRoot is called during the normal program operation in which
    // generational GC is using left heap for young objects
    bool slotIsYoung  = isInYoungHeap(objectSlot);

    if (! slotIsYoung) {
        // Slot is either in old generation or in static roots
        if (isInStaticHeap(objectSlot))
            return BakerMemoryManager::checkRoot(value, objectSlot);

        TObject* previousValue = *objectSlot;

        bool valueIsYoung = isInYoungHeap(value);
        bool previousValueIsYoung = isInYoungHeap(previousValue);

        if (valueIsYoung) {
            if (! previousValueIsYoung) {
                addCrossgenReference(objectSlot);
                return true;
            }
        } else {
            if (previousValueIsYoung) {
                removeCrossgenReference(objectSlot);
                return true;
            }
        }
    }

    return false;
}

void GenerationalMemoryManager::addCrossgenReference(TObject** pointer)
{
    //printf("addCrossgenReference %p", pointer);
    m_crossGenerationalReferences.push_front( reinterpret_cast<TMovableObject**>(pointer) );
}

void GenerationalMemoryManager::removeCrossgenReference(TObject** pointer)
{
    TPointerIterator iPointer = m_crossGenerationalReferences.begin();
    for (; iPointer != m_crossGenerationalReferences.end(); ++iPointer) {
        if (*iPointer == reinterpret_cast<TMovableObject**>(pointer)) {
            m_crossGenerationalReferences.erase(iPointer);
            return;
        }
    }
}
