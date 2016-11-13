
include_directories(${CMAKE_SOURCE_DIR}/include)
add_library(interpreter
    interpreter.cpp
    runtime.cpp
    usual_opcodes.cpp
    special_opcodes.cpp
    primitive_opcodes.cpp
)
