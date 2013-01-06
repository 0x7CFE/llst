g++ -m32 -march=native -mtune=native src/*.cpp -Iinclude/ -o build/llst `llvm-config-3.1 --cxxflags` `llvm-config-3.1 --ldflags --libs jit`
