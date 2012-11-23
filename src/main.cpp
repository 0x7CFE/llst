#include <iostream>
#include <vm.h>

int main(int argc, char **argv) {
    cout << "sizeof(TObject*) = " << sizeof(TObject*) << endl;
    
    Image testImage;
    testImage.loadImage("../image/testImage");

//     TArray& keys = *globals.globalsObject->keys;
//     for (int i = 0; i < keys.getSize(); i++)
//     {
//         TSymbol* globalName = (TSymbol*) keys[i];
//         std::string sname((const char*) globalName->getBytes(), globalName->getSize());
//         cout << "Object>>" << sname << endl;
//     }
    
    cout << "Loading Object object" << endl;
    TClass* object = (TClass*) testImage.getGlobal("Object");
    
    TArray& methodNames = * object->methods->keys;
    cout << "Object has " << methodNames.getSize() << " methods" << endl;
    for (int i = 0; i < methodNames.getSize(); i++)
    {
        TSymbol* name = (TSymbol*) methodNames[i]; 
        std::string sname((const char*) name->getBytes(), name->getSize());
        cout << sname << endl;
    }
    
    return 0;
}
