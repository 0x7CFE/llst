/*
 *    NonCollectMemoryManager.cpp
 *
 *    Implementation of non-collecting memory manager.
 *    All memory requests are serviced by continious
 *    allocation of space which is never released.
 *
 *    Of course this is not a thing you want in the
 *    production code. However, it may be helpful in various
 *    test scenarios where small tasks are performed in one shot.
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.3
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

bool NonCollectMemoryManager::initializeHeap(size_t heapSize, size_t /*maxSize*/)
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
