#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

BakerMemoryManager::BakerMemoryManager() : 
    m_gcCount(0), m_allocsBeyondGC(0), m_heapSize(0), m_maxHeapSize(0), m_heapOne(0), m_heapTwo(0), 
    m_activeHeapOne(true), m_inactiveHeapBase(0), m_inactiveHeapPointer(0), 
    m_activeHeapBase(0), m_activeHeapPointer(0), m_staticHeapSize(0), 
    m_staticHeapBase(0), m_staticHeapPointer(0)
{ 
    
}

BakerMemoryManager::~BakerMemoryManager()
{
    // TODO Reset the external pointers to catch the null pointers if something goes wrong
    free(m_staticHeapBase);
    free(m_heapOne);
}

bool BakerMemoryManager::initializeStaticHeap(size_t heapSize)
{
    if (heapSize % 2 != 0)
        heapSize++;
    
    void* heap = malloc(heapSize);
    if (!heap)
        return false;

    memset(heap, 0, heapSize);
    
    m_staticHeapBase = (uint8_t*) heap;
    m_staticHeapPointer = (uint8_t*) heap + heapSize;
    
    return true;
}

bool BakerMemoryManager::initializeHeap(size_t heapSize, size_t maxHeapSize /* = 0 */)
{
    // To initialize properly we need a heap with an even size
    if (heapSize % 2 != 0)
        heapSize++;
    
    void* base = malloc(heapSize);
    if (!base)
        return false;

    memset(base, 0, heapSize);
    
    m_heapSize = heapSize;
    m_maxHeapSize = maxHeapSize;
    
    uint32_t mediane = heapSize / 2;
    m_heapOne = (uint8_t*) base;
    m_heapTwo = (uint8_t*) base + mediane;
    
    m_activeHeapOne = true;
    m_activeHeapBase = m_heapOne;
    m_activeHeapPointer = (uint8_t*) base + mediane;
    m_inactiveHeapBase = (uint8_t*) base + mediane;
    m_inactiveHeapPointer = (uint8_t*) base + heapSize;
    
    return true;
}

void* BakerMemoryManager::allocate(size_t requestedSize, bool* gcOccured /*= 0*/ )
{
    if (gcOccured)
        *gcOccured = false;
    
    size_t attempts = 2;
    while (attempts-- > 0) {
        if (m_activeHeapPointer - requestedSize < m_activeHeapBase) {
            collectGarbage();
            if (gcOccured)
                *gcOccured = true;
            continue;
        }
        
        m_activeHeapPointer -= requestedSize;
        void* result = m_activeHeapPointer;
        
        if (!gcOccured)
            m_allocsBeyondGC++;
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
    TMovableObject* oldPlace       = object;
    TMovableObject* previousObject = 0;
    TMovableObject* newPlace       = 0;
    TMovableObject* replacement    = 0;

    while (true) {
        
        // Stage 1. Walking down the tree. Keep stacking objects to be moved 
        // until we find one that we can handle
        while (true) {
            // Checking whether this is inline integer
            if (isSmallInteger((TObject*) oldPlace)) {
                // Inline integers are stored directly in the 
                // pointer space. All we need to do is just copy 
                // contents of the poiner to a new place
            
                replacement = oldPlace;
                oldPlace = previousObject;
                break;
            }
            
            // Checking if object is not in the old space
            if ( (oldPlace > (TMovableObject*) (m_inactiveHeapBase + m_heapSize / 2)) 
              || (oldPlace < (TMovableObject*) m_inactiveHeapPointer))
//             if ( (oldPlace < (TMovableObject*) m_inactiveHeapBase) 
//               || (oldPlace > (TMovableObject*) m_inactiveHeapPointer))
            {
                // Object does not belong to a heap.
                // Either it is located in static space or
                replacement = oldPlace;
                oldPlace = previousObject;
                break;
            }
            
            // Checking if object is already moved
            if (oldPlace->size.isRelocated()) {
                if (oldPlace->size.isBinary()) {
                    replacement = oldPlace->data[0];
                } else {
                    uint32_t index = oldPlace->size.getSize();
                    replacement = oldPlace->data[index];
                }
                oldPlace = previousObject;
                break;
            }
            
            // Checking whether we're dealing with the binary object
            if (oldPlace->size.isBinary()) {
                // Current object is binary.
                // Moving object to a new space, copying it's data
                // and finally walking up to the object's class
                
                // Size of binary data
                uint32_t dataSize = oldPlace->size.getSize();
                
                // We need to allocate space evenly, so calculating the 
                // actual size of the block being reserved for the moving object
                uint32_t slotSize = correctPadding(dataSize);
                
                // Allocating copy in new space
                m_activeHeapPointer -= (slotSize + 2) * sizeof(TMovableObject*);
                newPlace = (TMovableObject*) m_activeHeapPointer;
                newPlace->size.setSize(dataSize);
                newPlace->size.setBinary(true);
                
                // Copying byte data
                memcpy(newPlace->data, oldPlace->data, dataSize);
                
                // Marking original copy of object as relocated so it would not be processed again
                oldPlace->size.setRelocated(true);
                
                // During GC process temporarily using data[0] as indirection pointer
                // This will be corrected on the next stage of current GC operation
                newPlace->data[0] = previousObject;
                previousObject = oldPlace;
                oldPlace = oldPlace->data[0];
                previousObject->data[0] = newPlace;
                
                // On the next iteration we'll be processing the data[0] of the current object
                // which is actually class pointer in TObject. 
                // NOTE It is expected that class of binary object would be non binary
                //continue;
            } else {
                // Current object is not binary, i.e. this is an ordinary object
                // with fields that are either SmallIntegers or pointers to other objects
                
                uint32_t fieldsCount = oldPlace->size.getSize();
                m_activeHeapPointer -= (fieldsCount + 2) * sizeof (TMovableObject*);
                newPlace = (TMovableObject*) m_activeHeapPointer;
                newPlace->size.setSize(fieldsCount);
                oldPlace->size.setRelocated(true);
                
                // FIXME What the heck is going on here?
                //       What about copying object's fields?
                const uint32_t lastObjectIndex = fieldsCount;
                newPlace->data[lastObjectIndex] = previousObject;
                previousObject = oldPlace;
                oldPlace = oldPlace->data[lastObjectIndex];
                previousObject->data[lastObjectIndex] = newPlace;
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
            if (oldPlace == 0)
                return replacement;
            
            // Either binary object or the last value (field from the ordinary one?)
            if ( oldPlace->size.isBinary() || (oldPlace->size.getSize() == 0) ) {
                // Fixing up class pointer
                newPlace = oldPlace->data[0];
                
                previousObject = newPlace->data[0];
                newPlace->data[0] = replacement;
                oldPlace->data[0] = newPlace;
                
                replacement = newPlace;
                oldPlace = previousObject;
            } else {
                // last field from TObject
                uint32_t lastFieldIndex = oldPlace->size.getSize();
                newPlace = oldPlace->data[lastFieldIndex]; 
                previousObject = newPlace->data[lastFieldIndex];
                newPlace->data[lastFieldIndex] = replacement;
                
                // Recovering zero fields (FIXME do they really exist?)
                lastFieldIndex--;
                while((lastFieldIndex > 0) && (oldPlace->data[lastFieldIndex] == 0))
                {
                    newPlace->data[lastFieldIndex] = 0;
                    lastFieldIndex--;
                }
                
                // Storing the last visited index to the size
                // If it gets zero then all fields were moved
                oldPlace->size.setSize(lastFieldIndex);
                oldPlace->size.setRelocated(true);
                
                newPlace->data[lastFieldIndex] = previousObject;
                previousObject = oldPlace;
                oldPlace = oldPlace->data[lastFieldIndex];
                previousObject->data[lastFieldIndex] = newPlace;
                break;
            }
                
        }
    }
}

void BakerMemoryManager::collectGarbage()
{
    m_gcCount++;
    
    
    printf("Before collection: heap size %d, used %d, free %d\n", 
           m_heapSize / 2, 
           m_heapSize / 2 - (m_activeHeapPointer - m_activeHeapBase),
           m_activeHeapPointer - m_activeHeapBase);
    
    // First of all swapping the spaces
    if (m_activeHeapOne)
    {
        printf("\n(1)->(2)\n");
        m_activeHeapBase = m_heapTwo;
        m_inactiveHeapBase = m_heapOne;
    } else {
        printf("\n(2)->(1)\n");
        m_activeHeapBase = m_heapOne;
        m_inactiveHeapBase = m_heapTwo;
    }
    
    m_activeHeapOne = not m_activeHeapOne;
    
    m_inactiveHeapPointer = m_activeHeapPointer;
    m_activeHeapPointer = m_activeHeapBase + m_heapSize / 2;
    
    // Then, performing the collection. Seeking from the root
    // objects down the hierarchy to find active objects. 
    // Then moving them to the new active heap.

    // Here we need to check the rootStack, staticRoots and the VM execution context
    TStaticRootsIterator iRoot = m_staticRoots.begin();
    for (; iRoot != m_staticRoots.end(); ++iRoot)
    {
        //printf("GC: Processing root pointer %p pointing to %p\n", *iRoot, **iRoot);
        **iRoot = moveObject(**iRoot);
    }

    // Updating external references
    TPointerIterator iExternalPointer = m_externalPointers.begin();
    for (; iExternalPointer != m_externalPointers.end(); ++iExternalPointer) {
//         printf("GC: Processing external pointer %p pointing to %p\n", *iExternalPointer, **iExternalPointer);
        **iExternalPointer = moveObject(**iExternalPointer);
//         printf("GC: External pointer %p now points to %p\n", *iExternalPointer, **iExternalPointer);
    }
    
    printf("After collection: heap size %d, used %d, free %d\n", 
           m_heapSize / 2, 
           m_heapSize / 2 - (m_activeHeapPointer - m_activeHeapBase),
           m_activeHeapPointer - m_activeHeapBase);
    
}

bool BakerMemoryManager::isInStaticHeap(void* location)
{
    return (location >= m_staticHeapBase && location < m_staticHeapPointer);
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
