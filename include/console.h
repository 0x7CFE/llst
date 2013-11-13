/*
 *    console.h
 *
 *    Console tools
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

#include <readline/readline.h>
#include <trie.h>
#include <string>
#include <memory>

class CompletionEngine {
private:
    typedef rtv::Trie<char, bool> TCompletionTrie;

    TCompletionTrie m_completionTrie;
    TCompletionTrie::Iterator m_iCurrentProposal;

    static std::auto_ptr<CompletionEngine> s_instance;
public:
    CompletionEngine() : m_completionTrie('\0'), m_iCurrentProposal(m_completionTrie.end()) { }
    static CompletionEngine* Instance() { return s_instance.get(); }

    void addWord(const std::string& word) { m_completionTrie.insert(word.c_str(), true); }
    void getProposals(const std::string& prefix) { m_iCurrentProposal = m_completionTrie.startsWith(prefix.c_str()); }
    void getProposals(const char* prefix) { m_iCurrentProposal = m_completionTrie.startsWith(prefix); }
    bool hasMoreProposals() { return m_iCurrentProposal != m_completionTrie.end(); }
    std::string getNextProposal() {
        std::string result(m_iCurrentProposal->first);
        ++m_iCurrentProposal;
        return result;
    }
};

void initializeCompletion();
