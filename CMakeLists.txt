cmake_minimum_required(VERSION 2.8)

project(llst)

if(LLVM_VERSION)
    set (CONFIG_COMMAND "llvm-config-${LLVM_VERSION}")
else()
    set (CONFIG_COMMAND "llvm-config")
endif()

# TODO Check that llvm-config is found and working

execute_process(COMMAND ${CONFIG_COMMAND} --cxxflags
                OUTPUT_STRIP_TRAILING_WHITESPACE
                OUTPUT_VARIABLE LLVM_CXX_FLAGS)
                
execute_process(COMMAND ${CONFIG_COMMAND} --ldflags
                OUTPUT_STRIP_TRAILING_WHITESPACE
                OUTPUT_VARIABLE LLVM_LD_FLAGS)

execute_process(COMMAND ${CONFIG_COMMAND} --libs all
                OUTPUT_STRIP_TRAILING_WHITESPACE
                OUTPUT_VARIABLE LLVM_LIBS)

set (COMPILER_FLAGS "-m32 -march=native -mtune=native -flto -fno-rtti -funroll-loops -Wall ${LLVM_CXX_FLAGS} ")
set (LINKER_FLAGS   "-m32 -L/usr/lib32 -flto -Wall ${LLVM_LD_FLAGS}")

add_subdirectory(src)
add_subdirectory(include)
include_directories(include)

file(GLOB CPP_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "src/*.cpp")

add_executable(llst ${CPP_FILES})
target_link_libraries(llst ${LLVM_LIBS} -ldl -lpthread)
set_target_properties(llst PROPERTIES COMPILE_FLAGS "${COMPILER_FLAGS}")
set_target_properties(llst PROPERTIES LINK_FLAGS "${LINKER_FLAGS}")
