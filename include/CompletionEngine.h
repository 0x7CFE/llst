/*
 *    CompletionEngine.h
 *
 *    Console completion proposals engine
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

#if !defined(LLST_COMPLETION_ENGINE_H)
#define LLST_COMPLETION_ENGINE_H

#include <radix_tree/radix_tree.hpp>
#include <memory>

#include "types.h"

class CompletionEngine {
private:
    typedef radix_tree< std::string, int > TCompletionDatabase;
    typedef std::vector< TCompletionDatabase::iterator > TProposals;

    TCompletionDatabase m_completionDatabase;
    TProposals m_currentProposals;
    TProposals::iterator m_iCurrentProposal;
    int m_totalWords;

    static std::auto_ptr<CompletionEngine> s_instance;
public:
    CompletionEngine() : m_totalWords(0) { }
    static CompletionEngine* Instance() { return s_instance.get(); }

    void addWord(const std::string& word) { m_completionDatabase[word] = m_totalWords++; }
    void getProposals(const std::string& prefix) {
        m_currentProposals.clear();
        m_completionDatabase.prefix_match(prefix, m_currentProposals);
        m_iCurrentProposal = m_currentProposals.begin();
    }
    bool hasMoreProposals() { return m_iCurrentProposal != m_currentProposals.end(); }
    std::string getNextProposal() {
        std::string result((*m_iCurrentProposal)->first);
        ++m_iCurrentProposal;
        return result;
    }

    void initialize(TDictionary* globals);
    bool readline(const std::string& prompt, std::string& result);
    void addHistory(const std::string& line);
};

#endif
