#include <memory.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

GenerationalMemoryManager::~GenerationalMemoryManager()
{
    // Nothing to do here
}

void GenerationalMemoryManager::moveYoungObjects()
{
    TPointerIterator iYoungPointer = m_crossGenerationalReferences.begin();
    for (; iYoungPointer != m_crossGenerationalReferences.end(); ++iYoungPointer)
        moveObject(**iYoungPointer);
    
    m_crossGenerationalReferences.clear();
    
    // Updating external references. Typically these are pointers stored in the hptr<>
    TPointerIterator iExternalPointer = m_externalPointers.begin();
    for (; iExternalPointer != m_externalPointers.end(); ++iExternalPointer) {
        **iExternalPointer = moveObject(**iExternalPointer);
    }
    
    TStaticRootsIterator iRoot = m_staticRoots.begin();
    for (; iRoot != m_staticRoots.end(); ++iRoot) {
        if (isInYoungHeap(**iRoot))
            **iRoot = moveObject(**iRoot);
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
    m_totalCollectionDelay += (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec);
    
    m_collectionsCount++;
}

void GenerationalMemoryManager::collectLeftToRight()
{
    // Classic baker algorithm moves objects after swapping the spaces, 
    // but in our case we do not want to swap them now. Still, in order to
    // satisfy moveObjects() we do this temporarily and then revert the pointers
    // to the needed state.
    
    // Setting the heap two as active leaving the heap pointer as is
    m_activeHeapBase    = m_heapTwo;
    m_inactiveHeapBase  = m_heapOne;
    m_activeHeapPointer = m_inactiveHeapPointer; // heap two
    
    // Moving the objects from the left to the right heap
    // TODO In certain circumstances right heap may not have
    //      enough space to store all active objects from gen 0.
    //      This may happen if massive allocation took place before
    //      the collection was initiated. In this case we need to interrupt 
    //      the collection, then allocate a new larger heap and recollect 
    //      ALL objects from both spaces to a new allocated heap. 
    //      After that old space is freed and new is treated either as heap one
    //      or heap two depending on the size.
    moveYoungObjects();

    uint8_t* lastHeapTwoPointer = m_activeHeapPointer;
    
    // Now, all active objects are located in the space two 
    // (inactive space in terms of classic Baker).
    // Resetting the space one pointers to mark space as empty.
    m_activeHeapBase    = m_heapOne;
    m_activeHeapPointer = m_activeHeapBase + m_heapSize / 2;
    
    m_inactiveHeapPointer = lastHeapTwoPointer;
    m_inactiveHeapBase    = m_heapTwo;

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
    m_activeHeapPointer = m_heapOne + m_heapSize / 2;

    moveObjects();
    
    // Objects were moved from right heap to the left one.
    // Now right heap may be emptied by resetting the heap pointer

    // Resetting heap two
    m_inactiveHeapPointer = m_heapTwo + m_heapSize / 2;
    
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
    return (m_inactiveHeapPointer - m_inactiveHeapBase < m_heapSize / 8);
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
    return (location >= m_activeHeapPointer) && (location < m_activeHeapBase + m_heapSize / 2);
}

bool GenerationalMemoryManager::checkRoot(TObject* value, TObject** objectSlot)
{
    bool inheritedResult = BakerMemoryManager::checkRoot(value, objectSlot);
    
    // checkRoot is called during the normal program operation in which
    // generational GC is using left heap for young objects
    bool valueIsYoung = isInYoungHeap(value);
    bool slotIsYoung  = isInYoungHeap(objectSlot);
    
    if (! slotIsYoung) {
        // Slot is either in old generation or in static roots
        
        TObject* previousValue = *objectSlot;
        bool previousValueIsYoung   = isInYoungHeap(previousValue);
        
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
    
    return inheritedResult;
}

void GenerationalMemoryManager::addCrossgenReference(TObject** pointer) 
{
    //printf("addCrossgenReference %p", pointer);
    m_crossGenerationalReferences.push_front((TMovableObject**) pointer);
}

void GenerationalMemoryManager::removeCrossgenReference(TObject** pointer)
{
    TPointerIterator iPointer = m_crossGenerationalReferences.begin();
    for (; iPointer != m_crossGenerationalReferences.end(); ++iPointer) {
        if (*iPointer == (TMovableObject**) pointer) {
            m_crossGenerationalReferences.erase(iPointer);
            return;
        }
    }
}
