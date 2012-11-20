#include <vm.h>

TObject* Image::getGlobal(const char* name)
{
    TDictionary* globalsDictionary = globals.globalsObject;
    TObject* result = globalsDictionary->find(name);
    return result;
}

