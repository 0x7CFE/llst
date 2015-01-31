/*
 *    llstPass.h
 *
 *    Optimizing LLVM pass for JIT code generator.
 *    Tries to remove redundant memory loads and roots.
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

#include <llvm/Pass.h>

llvm::FunctionPass* createLLSTPass();
