#include <memory.h>
#include <cstdlib>
#include <cstring>

NonCollectMemoryManager::NonCollectMemoryManager() :
    m_heapSize(0), m_heapBase(0), m_heapPointer(0),
    m_staticHeapSize(0), m_staticHeapBase(0), m_staticHeapPointer(0)
{
}

NonCollectMemoryManager::~NonCollectMemoryManager()
{
    free(m_staticHeapBase);
    for(std::size_t i = 0; i < m_usedHeaps.size(); i++)
        free( m_usedHeaps[i] );
}

bool NonCollectMemoryManager::initializeStaticHeap(size_t staticHeapSize)
{
    staticHeapSize = correctPadding(staticHeapSize);
    uint8_t* heap = static_cast<uint8_t*>( std::malloc(staticHeapSize) );
    if (!heap)
        return false;

    std::memset(heap, 0, staticHeapSize);

    m_staticHeapBase = heap;
    m_staticHeapPointer = heap + staticHeapSize;
    m_heapSize = staticHeapSize;

    return true;
}

bool NonCollectMemoryManager::initializeHeap(size_t heapSize, size_t maxSize)
{
    heapSize = correctPadding(heapSize);
    uint8_t* heap = static_cast<uint8_t*>( std::malloc(heapSize) );
    if (!heap)
        return false;

    std::memset(heap, 0, heapSize);

    m_heapBase = heap;
    m_heapPointer = heap + heapSize;
    m_heapSize = heapSize;

    m_usedHeaps.push_back(heap);

    return true;
}


void NonCollectMemoryManager::growHeap()
{
    uint8_t* heap = static_cast<uint8_t*>( std::malloc(m_heapSize) );
    if (!heap) {
        std::printf("MM: Cannot allocate %zu bytes\n", m_heapSize);
        abort();
    }

    std::memset(heap, 0, m_heapSize);

    m_heapBase = heap;
    m_heapPointer = heap + m_heapSize;

    m_usedHeaps.push_back(heap);
}

void* NonCollectMemoryManager::allocate(size_t requestedSize, bool* gcOccured /*= 0*/ )
{
    if (gcOccured)
        *gcOccured = false;

    if (m_heapPointer - requestedSize < m_heapBase) {
        growHeap();

        if (gcOccured)
            *gcOccured = true;
    }

    m_heapPointer -= requestedSize;
    return m_heapPointer;
}

void* NonCollectMemoryManager::staticAllocate(size_t requestedSize)
{
    uint8_t* newPointer = m_staticHeapPointer - requestedSize;
    if (newPointer < m_staticHeapBase)
    {
        std::fprintf(stderr, "Could not allocate %u bytes in static heaps\n", requestedSize);
        return 0;
    }
    m_staticHeapPointer = newPointer;
    return newPointer;
}

bool NonCollectMemoryManager::isInStaticHeap(void* location)
{
    return (location >= m_staticHeapPointer) && (location < m_staticHeapBase + m_staticHeapSize);
}
