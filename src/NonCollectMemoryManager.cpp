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
#include <cstdlib>
#include <cstring>
#include <iostream>

NonCollectMemoryManager::NonCollectMemoryManager() :
    m_heapSize(0), m_heapBase(0), m_heapPointer(0), m_memoryInfo(),
    m_staticHeapSize(0), m_staticHeapBase(0), m_staticHeapPointer(0)
{
    m_memoryInfo.heapSize = 0;
    m_memoryInfo.freeHeapSize = 0;
    m_memoryInfo.allocationTimer.start();
    m_memoryInfo.heapIncreaseTimer.start();

    // TODO set everything in m_memoryInfo to 0
    m_logFile.open("gc.log", std::fstream::out);
}

NonCollectMemoryManager::~NonCollectMemoryManager()
{
    free(m_staticHeapBase);
    for(std::size_t i = 0; i < m_usedHeaps.size(); i++)
        free( m_usedHeaps[i] );
}

void NonCollectMemoryManager::writeLogLine(TMemoryManagerEvent event){
    m_logFile << event.begin.toString(SNONE, 3)
            << ": [" << event.eventName << " ";
    if(!event.heapInfo.empty()){
        TMemoryManagerHeapInfo eh = event.heapInfo;
        m_logFile << eh.usedHeapSizeBeforeCollect << "K->"
                << eh.usedHeapSizeAfterCollect << "K("
                << eh.totalHeapSize << "K)";
        for(std::list<TMemoryManagerHeapEvent>::iterator i = eh.heapEvents.begin(); i != eh.heapEvents.end(); i++){
            m_logFile << "[" << i->eventName << ": "
                    << i->usedHeapSizeBeforeCollect << "K->"
                    << i->usedHeapSizeAfterCollect << "K("
                    << i->totalHeapSize << "K)";
            if(!i->timeDiff.isEmpty()){
                m_logFile << ", " << i->timeDiff.toString(SSHORT, 6);
            }
            m_logFile << "] ";
        }
    }
    if(!event.timeDiff.isEmpty()){
        m_logFile << ", " << event.timeDiff.toString(SSHORT, 6);
    }
    m_logFile << "]\n";
    m_logFile.flush();
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
    TMemoryManagerEvent event;
    event.heapInfo.usedHeapSizeBeforeCollect = m_usedHeaps.size()*m_heapSize/1024;
    m_memoryInfo.heapSize += m_usedHeaps.size()*m_heapSize*(m_memoryInfo.heapIncreaseTimer.get<TMicrosec>().toInt()/1000000.0) / 1024.0;
    m_memoryInfo.heapIncreaseTimer.start();
    m_memoryInfo.collectionsCount++;
    event.eventName = "GC";
    event.begin = m_memoryInfo.timer.get<TSec>();
    uint8_t* heap = static_cast<uint8_t*>(std::malloc(m_heapSize));
    if (!heap) {
        std::printf("MM: Cannot allocate %zu bytes\n", m_heapSize);
        abort();
    }

    std::memset(heap, 0, m_heapSize);

    m_heapBase = heap;
    m_heapPointer = heap + m_heapSize;

    m_usedHeaps.push_back(heap);

    //there is no collecting: after = before
    event.heapInfo.usedHeapSizeAfterCollect = event.heapInfo.usedHeapSizeBeforeCollect;
    event.heapInfo.totalHeapSize = m_usedHeaps.size()*m_heapSize/1024;
    event.timeDiff = m_memoryInfo.timer.get<TSec>() - event.begin;
    m_memoryInfo.totalCollectionDelay += event.timeDiff.convertTo<TMicrosec>().toInt();
    m_memoryInfo.events.push_front(event);
    writeLogLine(event);
}

void* NonCollectMemoryManager::allocate(size_t requestedSize, bool* gcOccured /*= 0*/ )
{

    //TODO remove from this place or add compilation flag
    double temp = (m_heapPointer - requestedSize - m_heapBase)*
            (m_memoryInfo.allocationTimer.get<TMicrosec>().toInt()/1000000.0) / 1024.0;
    m_memoryInfo.freeHeapSize += temp;
    //std::cout << "alloc " << m_memoryInfo.freeHeapSize << ' ' << temp << ' ' << m_memoryInfo.allocationTimer.get<TMicrosec>().toInt()/1000000.0 << '\n';
    m_memoryInfo.allocationTimer.start();
    if (gcOccured)
        *gcOccured = false;

    if (m_heapPointer - requestedSize < m_heapBase) {
        growHeap();

        if (gcOccured)
            *gcOccured = true;
    }

    m_heapPointer -= requestedSize;

    m_memoryInfo.allocationsCount++;
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

TMemoryManagerInfo NonCollectMemoryManager::getStat() {
    m_memoryInfo.heapSize += (m_usedHeaps.size()*m_heapSize/1024)*(m_memoryInfo.heapIncreaseTimer.get<TMicrosec>().toInt()/1000000.0) / 1024.0;
    std::cout << m_memoryInfo.heapSize <<'\n';
    m_memoryInfo.freeHeapSize += (m_heapPointer - m_heapBase)*
            (m_memoryInfo.allocationTimer.get<TMicrosec>().toInt()/1000000.0) / 1024.0;
    return m_memoryInfo;
}