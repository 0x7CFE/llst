/*
 *    LLVMMemoryManager.cpp
 *
 *    Implementation of the MM aware of LLVM specifics
 *    such as function stack traversing.
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

// This will be used by llvm functions to store frame stack info
extern "C" { LLVMMemoryManager::TStackEntry* llvm_gc_root_chain = 0; }

void LLVMMemoryManager::moveObjects()
{
    // First of all doing our usual job
    BakerMemoryManager::moveObjects();

    // Then, traversing the call stack pointers
    for (TStackEntry* entry = llvm_gc_root_chain; entry != 0; entry = entry->next) {
        const uint32_t metaCount = entry->map->numMeta;
        const uint32_t rootCount = entry->map->numRoots;
        uint32_t entryIndex = 0;

        // Processing stack objects
        for (; entryIndex < metaCount; entryIndex++) {
            const TMetaInfo* metaInfo = static_cast<const TMetaInfo*>( entry->map->meta[entryIndex] );
            if (metaInfo && metaInfo->isStackObject) {
                TMovableObject* stackObject = reinterpret_cast<TMovableObject*>( entry->roots[entryIndex] );

                if (! stackObject)
                    continue;

                // Stack objects are allocated on a stack frames of jit functions
                // We need to process only their fields and class pointer
                for (uint32_t fieldIndex = 0; fieldIndex < stackObject->size.getSize() + 1; fieldIndex++) {

                    TMovableObject* field = stackObject->data[fieldIndex];
                    if (field)
                        field = moveObject(field);

                    stackObject->data[fieldIndex] = field;
                }
            }
        }

        // Iterating through the normal roots in the current stack frame
        for (; entryIndex < rootCount; entryIndex++) {
            TMovableObject* object = reinterpret_cast<TMovableObject*>( entry->roots[entryIndex] );

            if (object != 0)
                object = moveObject(object);

            entry->roots[entryIndex] = object;
        }
    }
}

LLVMMemoryManager::LLVMMemoryManager()
{
}

LLVMMemoryManager::~LLVMMemoryManager()
{

}
