#include <memory.h>

GenerationalMemoryManager::~GenerationalMemoryManager()
{
    // Nothing to do here
}

void GenerationalMemoryManager::collectGarbage()
{
    // Generational GC takes advantage of a fact that most of objects
    // live in a very short amount of time. And those who survived the 
    // first collection are typically stay there for a long time.
    // In classic Baker collector both spaces are equal in rights and
    // are used interchangebly. In Generational GC right space is selected
    // as a storage for long living generation 1 whereas immediate generation 0 
    // objects are repeatedly allocated in the space one even after collection.
    
    // This is a normal, most frequent collection mode.
    // In this mode we move generation 0 objects from the
    // left heap (heap one) to the right heap (heap two)
    // so they'll become a generation 1 objects.
    //
    // After objects are moved two further scenarios exist:
    //
    // 1. Normally, heap one is cleared and again used for further
    // allocations. Collector state is then set to csRightSpaceActive.
    //
    // 2. If amount of free space in the heap two is below threshold,
    // additional collection takes place which moves all objects
    // to the left space and resets the state to csRightSpaceEmpty.

    collectLeftToRight();
    if (checkThreshold())
        collectRightToLeft();
    
    /* switch (m_currentState) {
        case csRightSpaceEmpty:
            collectLeftToRight();
            break;

        case csRightSpaceActive:
            collectLeftToRight();
            checkThreshold();
            break;

        case csRightCollect:
            collectRightToLeft();
            break;

        default:
            fprintf(stderr, "GMM: Invalid state code %d", (uint32_t) m_currentState);
    } */
}

void GenerationalMemoryManager::collectLeftToRight()
{
    // Classic baker algorithm moves objects after swapping the spaces, 
    // but in our case we do not want to swap them now. Still in order to
    // satisfy moveObjects() we do this temporarily and then revert the pointers
    // to the needed state.
    uint8_t* storedHeapOnePointer = m_activeHeapPointer;
    uint8_t* storedHeapTwoPointer = m_inactiveHeapPointer;
    
    // Setting the heap two as active leaving the heap pointer as is
    m_activeHeapBase   = m_heapTwo;
    m_inactiveHeapBase = m_heapOne;
    m_inactiveHeapPointer = m_activeHeapPointer;
    
    // Moving the objects
    moveObjects();

    uint8_t* lastHeapTwoPointer = m_activeHeapPointer;
    
    // Now, all active objects are located in the space two 
    // (inactive space in terms of classic Baker).
    // Resetting the space one pointers so that it becomes empty.
    m_activeHeapBase    = m_heapOne;
    m_activeHeapPointer = m_activeHeapBase + m_heapSize / 2;
    
    m_inactiveHeapPointer = lastHeapTwoPointer;
    m_inactiveHeapBase    = m_heapTwo;

    // After this operation active objects from space one now all
    // in space two and are treated as generation 1.

    // If no objects were moved (all were collected) then we do not need
    // to switch the collector state because heap two remains empty
//     if ((m_currentState == csRightSpaceEmpty) && (storedHeapTwoPointer != lastHeapTwoPointer)) {
//         // Some objects moved to the space two
//         m_currentState = csRightSpaceActive;
//     }
}

void GenerationalMemoryManager::collectRightToLeft()
{
    uint8_t* storedHeapOnePointer = m_activeHeapPointer;
    uint8_t* storedHeapTwoPointer = m_inactiveHeapPointer;

    m_activeHeapBase    = m_heapOne;
    m_inactiveHeapBase  = m_heapTwo;
    m_activeHeapPointer = m_heapOne + m_heapSize / 2;

    moveObjects();

    // Resetting heap two
    m_inactiveHeapPointer = m_heapTwo + m_heapSize / 2;
}

void GenerationalMemoryManager::checkThreshold()
{
    return (m_inactiveHeapPointer - m_inactiveHeapBase < m_heapSize / 8);
}
