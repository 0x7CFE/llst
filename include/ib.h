/*
 *    ib.h
 *
 *    Interface for the native method compiler
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
#include <types.h>
#include <map>

namespace ib {

struct ImageClass {
    std::string name;
    std::string parent;
    std::vector<std::string> instanceVariables;
    std::map<std::string, ImageMethod> methods;
};

struct ImageMethod {
    std::string className;
    std::string name;
    std::vector<std::string> temporaries;
    std::vector<std::string> arguments;
    std::vector<uint8_t> bytecodes;
};

class ImageBuilder {
private:
    std::map<std::string, ImageClass> m_imageObjects;

public:
};

class MethodCompiler {
private:
    ImageMethod m_currentMethod;

public:
    bool compile(const std::string& className, const std::string& methodSource);
};

}