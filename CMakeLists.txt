cmake_minimum_required(VERSION 2.8)

project(llst)

#     SET (CMAKE_CXX_COMPILER             "/usr/bin/clang++")
#     SET (CMAKE_CXX_FLAGS                "-m32 -Wall -O3 -DNDEBUG")
#     SET (CMAKE_CXX_FLAGS_DEBUG          "-g")
#     SET (CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
#     SET (CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
#     SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
# 
#     SET (CMAKE_AR      "/usr/bin/llvm-ar")
#     SET (CMAKE_LINKER  "/usr/bin/llvm-ld")
#     SET (CMAKE_NM      "/usr/bin/llvm-nm")
#     SET (CMAKE_OBJDUMP "/usr/bin/llvm-objdump")
#     SET (CMAKE_RANLIB  "/usr/bin/llvm-ranlib")

add_subdirectory(src)
add_subdirectory(include)

file(GLOB CPP_FILES "src/*.cpp")

add_executable(llst ${CPP_FILES})

set (COMPILER_FLAGS "-O3 -m32 -march=native -mfpmath=sse -Ofast -flto -march=native -funroll-loops -Wall")
set (LINKER_FLAGS "-m32 -L/usr/lib32 -Wall")

set_target_properties(llst PROPERTIES COMPILE_FLAGS "${COMPILER_FLAGS}")
set_target_properties(llst PROPERTIES LINK_FLAGS "${LINKER_FLAGS}")

include_directories(include)

