#include <iostream>
#include <vm.h>

int main(int argc, char **argv) {
    TString* s = newObject<TString>(10);
    TString::isBinary();
    
    std::cout << "Hello, world!" << std::endl;
    return 0;
}
