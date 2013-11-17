/*
 *    primitives.h
 *
 *    Primitive handling functions which are part of soft VM
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.1
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

#ifndef LLST_PRIMITIVES_H_INCLUDED
#define LLST_PRIMITIVES_H_INCLUDED

#include <types.h>

TObject* callPrimitive(uint8_t opcode, TObjectArray* arguments, bool& primitiveFailed);
TObject* callSmallIntPrimitive(uint8_t opcode, int32_t leftOperand, int32_t rightOperand, bool& primitiveFailed);
TObject* callIOPrimitive(uint8_t opcode, TObjectArray& args, bool& primitiveFailed);

#endif
