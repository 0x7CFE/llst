#include "patterns/DecodeBytecode.h"
#include "helpers/ControlGraph.h"
#include <vm.h>

#include <memory>

TEST_P(P_DecodeBytecode, DecodeMethod)
{
    /* The method has already been decoded.
     * Now we check the method for its correctness.
     */
    H_CheckCFGCorrect(m_cfg);
}

typedef std::vector< std::tr1::tuple<std::string /*name*/, std::string /*bytecode*/> > MethodsT;
MethodsT getMethods();

INSTANTIATE_TEST_CASE_P(_, P_DecodeBytecode, ::testing::ValuesIn(getMethods()) );

MethodsT getMethods()
{
    std::auto_ptr<IMemoryManager> memoryManager(new BakerMemoryManager());
    memoryManager->initializeHeap(1024*1024, 1024*1024);
    std::auto_ptr<Image> smalltalkImage(new Image(memoryManager.get()));
    smalltalkImage->loadImage(TESTS_DIR "./data/DecodeAllMethods.image");

    MethodsT imageMethods;
    TDictionary* imageGlobals = globals.globalsObject;
    for (uint32_t i = 0; i < imageGlobals->keys->getSize(); i++)
    {
        TSymbol* key   = (*imageGlobals->keys)[i];
        TObject* value = (*imageGlobals->values)[i];

        std::string keyString = key->toString();
        char firstLetter = keyString[0];
        if ( keyString != "Smalltalk" && std::isupper(firstLetter) ) {
            TClass* currentClass = static_cast<TClass*>(value);
            std::string className = currentClass->name->toString();

            TSymbolArray* names = currentClass->methods->keys;
            TObjectArray* methods = currentClass->methods->values;
            for (uint32_t m = 0; m < methods->getSize(); m++) {
                std::string methodName = (*names)[m]->toString();
                TMethod* method = static_cast<TMethod*>( (*methods)[m] );
                std::string bytecode = std::string(reinterpret_cast<const char*>(method->byteCodes->getBytes()), method->byteCodes->getSize());
                imageMethods.push_back( std::tr1::make_tuple(className + ">>" + methodName, bytecode) );
            }
        }
    }
    return imageMethods;
}
