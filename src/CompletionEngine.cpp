/*
 *    CompletionEngine.cpp
 *
 *    Smalltalk aware completion functions for readline library
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

#include <CompletionEngine.h>
#include <readline/readline.h>

std::auto_ptr<CompletionEngine> CompletionEngine::s_instance(new CompletionEngine);

static char* smalltalk_generator(const char* text, int state) {
    CompletionEngine* completionEngine = CompletionEngine::Instance();

    if (state == 0)
        completionEngine->getProposals(text);

    if (completionEngine->hasMoreProposals())
        return strdup(completionEngine->getNextProposal().c_str());
    else
        return 0;
}

static char** smalltalk_completion(const char* text, int start, int end) {
    return rl_completion_matches(text, smalltalk_generator);
}

static void initializeReadline()
{
    rl_readline_name = "llst";
    rl_attempted_completion_function = smalltalk_completion;
}

void CompletionEngine::initialize(TDictionary* globals)
{
    // Binding completion helpers to the readline subsystem
    initializeReadline();

    // Populating completion database with globals
    for (uint32_t i = 0; i < globals->keys->getSize(); i++) {
        TSymbol* key   = (*globals->keys)[i];
        TObject* value = (*globals->values)[i];

        std::string keyString = key->toString();
        char firstLetter = keyString[0];
        if ( keyString != "Smalltalk" && std::isupper(firstLetter) ) {
            TClass* currentClass = static_cast<TClass*>(value);

            // Adding class name
            addWord(currentClass->name->toString());

            // Acquiring selectors of class methods
            TSymbolArray* selectors = currentClass->methods->keys;
            const uint32_t keysSize = selectors->getSize();

            // Adding selectors
            for ( uint32_t methodIndex = 0; methodIndex < keysSize; methodIndex++) {
                const std::string methodName = (*selectors)[methodIndex]->toString();

                // Adding method name
                addWord(methodName);
            }

            // Adding metaclass name and methods
            TClass* metaClass = currentClass->getClass();
            //addWord(metaClass->name->toString());

            TSymbolArray*  metaSelectors = metaClass->methods->keys;
            const uint32_t metaSize = metaSelectors->getSize();
            for (uint32_t methodIndex = 0; methodIndex < metaSize; methodIndex++) {
                const std::string methodName = (*metaSelectors)[methodIndex]->toString();

                // Adding meta method name
                addWord(methodName);
            }
        }
    }
}
