cmake_minimum_required(VERSION 2.8)

project(llst)

add_subdirectory(src)
add_subdirectory(include)

file(GLOB CPP_FILES "src/*.cpp")

add_executable(llst ${CPP_FILES})

if(NOT LLVM_CXX_FLAGS)
   exec_program("llvm-config-3.0 --cxxflags"
                OUTPUT_VARIABLE LLVM_CXX_FLAGS)
   set(LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}" CACHE STRING "LLVM Compiler flags")
endif()

if(NOT LLVM_LD_FLAGS)
   exec_program("llvm-config-3.0 --ldflags"
                OUTPUT_VARIABLE LLVM_LD_FLAGS)
   set(LLVM_LD_FLAGS "${LLVM_LD_FLAGS}" CACHE STRING "LLVM Linker flags")
endif()

SET (CMAKE_CXX_COMPILER             "/usr/bin/clang++")
SET (CMAKE_CXX_FLAGS                "-g -m32 -Wall -O3 ${COMPILER_FLAGS}")

SET (CMAKE_CXX_FLAGS_DEBUG          "-g")
SET (CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
SET (CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

SET (CMAKE_AR      "/usr/bin/llvm-ar")
SET (CMAKE_LINKER  "/usr/bin/llvm-link")
SET (CMAKE_NM      "/usr/bin/llvm-nm")
SET (CMAKE_OBJDUMP "/usr/bin/llvm-objdump")
SET (CMAKE_RANLIB  "/usr/bin/llvm-ranlib")

set (COMPILER_FLAGS "-O3 -m32 -march=native -mtune=native -flto -funroll-loops -Wall ${LLVM_CXX_FLAGS}")
set (LINKER_FLAGS "-m32 -flto -L/usr/lib32 -Wall -lllvm ${LLVM_LD_FLAGS}")
# set (COMPILER_FLAGS "-O3 -m32 -march=native -mtune=native -Ofast -flto -funroll-loops -Wall ${LLVM_CXX_FLAGS}")
# set (LINKER_FLAGS "-m32 -flto -L/usr/lib32 -Wall -lllvm ${LLVM_LD_FLAGS}")

set_target_properties(llst PROPERTIES COMPILE_FLAGS "${COMPILER_FLAGS}")
set_target_properties(llst PROPERTIES LINK_FLAGS "${LINKER_FLAGS}")

include_directories(include)

