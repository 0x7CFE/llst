/*
 *    TDictionary.cpp 
 *    
 *    Implementation of TDictionary lookup methods
 *    
 *    LLST (LLVM Smalltalk or Lo Level Smalltalk) version 0.1
 *    
 *    LLST is 
 *        Copyright (C) 2012 by Dmitry Kashitsyn   aka Korvin aka Halt <korvin@deeptown.org>
 *        Copyright (C) 2012 by Roman Proskuryakov aka Humbug          <humbug@deeptown.org>
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

#include <types.h>
#include <string.h>
#include <algorithm>

bool TDictionary::compareSymbols::operator() (const TSymbol* left, const TSymbol* right) const
{
    const uint8_t* leftBase = left->getBytes();
    const uint8_t* leftEnd  = leftBase + left->getSize();
    
    const uint8_t* rightBase = right->getBytes();
    const uint8_t* rightEnd  = rightBase + right->getSize();
    
    return std::lexicographical_compare(leftBase, leftEnd, rightBase, rightEnd);
}

bool TDictionary::compareSymbols::operator() (const TSymbol* left, const char* right) const
{
    const uint8_t* leftBase = left->getBytes();
    const uint8_t* leftEnd  = leftBase + left->getSize();
    
    return std::lexicographical_compare(leftBase, leftEnd, right, right + strlen(right));
}

bool TDictionary::compareSymbols::operator() (const char* left, const TSymbol* right) const
{
    return !operator()(right, left);
}

TObject* TDictionary::find(const TSymbol* key) const
{
    // Keys are stored in order
    // Thus we may apply binary search
    const TDictionary::compareSymbols less;
    TSymbol** keysBase = (TSymbol**) keys->getFields();
    TSymbol** keysLast = keysBase + keys->getSize();
    TSymbol** foundKey = std::lower_bound(keysBase, keysLast, key, less);
    
    if (foundKey != keysLast && !less(key, *foundKey))
    {
        std::ptrdiff_t index = foundKey - keysBase;
        return values->getField(index);
    } else
        return 0; // key not found
}

TObject* TDictionary::find(const char* key) const
{
    // Keys are stored in order
    // Thus we may apply binary search
    const TDictionary::compareSymbols less;
    TSymbol** keysBase = (TSymbol**) keys->getFields();
    TSymbol** keysLast = keysBase + keys->getSize();
    TSymbol** foundKey = std::lower_bound(keysBase, keysLast, key, less);
    
    if (foundKey != keysLast && !less(key, *foundKey))
    {
        std::ptrdiff_t index = foundKey - keysBase;
        return values->getField(index);
    } else
        return 0; // key not found
}
