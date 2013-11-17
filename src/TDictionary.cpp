/*
 *    TDictionary.cpp
 *
 *    Implementation of TDictionary lookup methods
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

#include <types.h>
#include <algorithm>

template<typename K>
TObject* TDictionary::find(const K* key) const
{
    // Keys are stored in order
    // Thus we may apply binary search
    const TSymbol::TCompareFunctor compare;
    TSymbol** keysBase = reinterpret_cast<TSymbol**>( keys->getFields() );
    TSymbol** keysLast = keysBase + keys->getSize();
    TSymbol** foundKey = std::lower_bound(keysBase, keysLast, key, compare);

    // std::lower_bound returns an element which is >= key,
    // we have to check whether the found element is not > key.
    if (foundKey != keysLast && !compare(key, *foundKey)) {
        std::ptrdiff_t index = std::distance(keysBase, foundKey);
        return values->getField(index);
    } else
        return 0; // key not found
}

template TObject* TDictionary::find<char>(const char* key) const;
template TObject* TDictionary::find<TSymbol>(const TSymbol* key) const;
