#include <memory.h>
#include <stdio.h>

void* BakerMemoryManager::allocateMemory(size_t requestedSize)
{
    size_t attempts = 3;
    while (attempts-- > 0) {
        if (m_activeHeapPointer - requestedSize < m_activeHeapBase) {
            collectGarbage();
            continue;
        }
        
        m_activeHeapPointer -= requestedSize;
        void* result = m_activeHeapPointer;
        return result;
    }
    
    fprintf(stderr, "Could not allocate %d bytes in heap after 3 attempts\n", requestedSize);
    return 0;
}

TMovableObject* BakerMemoryManager::moveObject(TMovableObject* object)
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
                // Yes it is. Because inline integers are stored
                // directly in the pointer space all we need to do
                // is just copy contents of the poiner to a new place
            
                replacement = oldPlace;
                oldPlace = previousObject;
                break;
            }
            
            // Checking if object is not in old space
            if ( (oldPlace < (TMovableObject*) m_inactiveHeapBase) 
              || (oldPlace > (TMovableObject*) m_inactiveHeapPointer))
            {
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
                
                // Actual size of binary data
                uint32_t dataSize = oldPlace->size.getSize();
                
                // We need to allocate space evenly, so calculating the 
                // actual size of the block being reserved for the moving object
                //const uint32_t padding = sizeof(TMovableObject*) - 1;
                uint32_t slotSize = correctPadding(dataSize); //(dataSize + padding) & ~padding;
                
                // Allocating copy in new space
                m_activeHeapPointer -= slotSize + 2 * sizeof(TMovableObject*);
                newPlace = (TMovableObject*) m_activeHeapPointer;
                newPlace->size.setSize(dataSize);
                newPlace->size.setBinary(true);
                
                // Copying byte data FIXME is it correct?
                memcpy(newPlace->data, oldPlace->data, slotSize + 2 * sizeof(TMovableObject));
                
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
                continue;
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
                
                // T_T
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
    m_activeHeapPointer = m_activeHeapBase + m_heapSize;
    
    // Then, performing the collection. Seeking from the root
    // objects down the hierarchy to find active objects. 
    // Then moving them to the new active heap.
    
    /* TObject* rootStack;   
    TObject* staticRoots;
    
    for (uint32_t i = 0; i < size; i++)
        rootStack[i] = moveObject(rootStack[i]);
    
    for (uint32_t i = 0; i < staticRootSize; i++)
        staticRoots[i] = moveObject(staticRoots[i]); */
    
    // TODO flush the method cache
}

