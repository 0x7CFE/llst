#include <iostream>
#include <vm.h>

int main(int argc, char **argv) {
    std::cout << "Testing image loading" << std::endl;
    
    Image testImage;
    testImage.loadImage("../image/testImage");
    
    return 0;
}
