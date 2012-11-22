#include <iostream>
#include <vm.h>

int main(int argc, char **argv) {
    std::cout << "Testing image loading" << std::endl;
    
    Image testImage;
    testImage.loadImage("../image/testImage");

        
    cout << "Globals:" << endl;
    for (int i = 0; i < globals.globalsObject->getSize(); i++)
    {
        TSymbol* globalName = (TSymbol*) (*globals.globalsObject->keys)[i];
        std::string sname((const char*) globalName->getBytes(), globalName->getSize());
        cout << "Object>>" << sname << endl;
    }
    
    cout << "Loading Object object" << endl;
    TClass* object = (TClass*) testImage.getGlobal("Object");
    TDictionary& methods = *object->methods;
    cout << "Object has " << methods.getSize() << " methods" << endl;
    for (int i = 0; i < methods.getSize(); i++)
    {
        TMethod* method = (TMethod*) methods[i];
        TSymbol* methodName = method->name;
        std::string sname((const char*) methodName->getBytes(), methodName->getSize());
        cout << "Object>>" << sname << endl;
    }
    
    return 0;
}
