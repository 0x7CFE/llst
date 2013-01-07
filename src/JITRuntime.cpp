/*
 *    main.cpp
 *
 *    Program entry point
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

#include <jit.h>

#include "llvm/Support/TargetSelect.h"
#include <llvm/Support/IRReader.h>
#include <llvm/Analysis/Verifier.h>

using namespace llvm;

void JITRuntime::initialize()
{
    // Initializing LLVM subsystem
    InitializeNativeTarget();
    
    LLVMContext& Context = getGlobalContext();

    // Initializing types module
    SMDiagnostic Err;
    m_TypeModule = ParseIRFile("../include/llvm_types.ll", Err, Context);
    
    // Initializing JIT module.
    // All JIT functions will be created here
    m_JITModule = new Module("jit", Context);

    // Initializing method compiler
    m_methodCompiler = new MethodCompiler(m_JITModule, m_TypeModule);
    
    std::string error;
    m_executionEngine = EngineBuilder(m_JITModule).setEngineKind(EngineKind::JIT).setErrorStr(&error).create();
    if(!m_executionEngine) {
        printf("%s\n", error.c_str());
        exit(1);
    }
}

void JITRuntime::dumpJIT()
{
    verifyModule(*m_JITModule);
    m_JITModule->dump();
}

JITRuntime::~JITRuntime() {
    // TODO Finalize stuff and dispose memory
}
