cmake_minimum_required(VERSION 2.8)

project(llst)

# # A convenience variable:
# set(LLVM_ROOT "/usr/lib/llvm-3.1" CACHE PATH "Root of LLVM install.")
# 
# # A bit of a sanity check:
# if( NOT EXISTS ${LLVM_ROOT}/include/llvm )
# message(FATAL_ERROR "LLVM_ROOT (${LLVM_ROOT}) is not a valid LLVM install")
# endif()
# 
# # We incorporate the CMake features provided by LLVM:
# set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_ROOT}/share/llvm/cmake")
# include(LLVMConfig)
# 
# # Now set the header and library paths:
# include_directories( ${LLVM_INCLUDE_DIRS} )
# link_directories( ${LLVM_LIBRARY_DIRS} )
# add_definitions( ${LLVM_DEFINITIONS} )
# 
# # Let's suppose we want to build a JIT compiler with support for
# # binary code (no interpreter):
# llvm_map_components_to_libraries(REQ_LLVM_LIBRARIES jit native)
# 

# SET (CMAKE_CXX_COMPILER             "/usr/bin/clang++")
# SET (CMAKE_CXX_FLAGS                "-g -m32 -Wall")
# SET (CMAKE_CXX_FLAGS_DEBUG          "-g")
# SET (CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
# SET (CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
# SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
# 
# SET (CMAKE_AR      "/usr/bin/llvm-ar")
# SET (CMAKE_LINKER  "/usr/bin/llvm-link")
# SET (CMAKE_NM      "/usr/bin/llvm-nm")
# SET (CMAKE_OBJDUMP "/usr/bin/llvm-objdump")
# SET (CMAKE_RANLIB  "/usr/bin/llvm-ranlib")

execute_process(COMMAND llvm-config-3.1 --cxxflags
                OUTPUT_STRIP_TRAILING_WHITESPACE
                OUTPUT_VARIABLE LLVM_CXX_FLAGS)
                
execute_process(COMMAND llvm-config-3.1 --ldflags --libs jit
                OUTPUT_STRIP_TRAILING_WHITESPACE
                OUTPUT_VARIABLE LLVM_LD_FLAGS)
                
set (COMPILER_FLAGS "-m32 -march=native -mtune=native -funroll-loops -Wall ${LLVM_CXX_FLAGS} ")
set (LINKER_FLAGS   "-m32 -L/usr/lib32 -Wall ${LLVM_LD_FLAGS} ")

add_subdirectory(src)
add_subdirectory(include)
include_directories(include)

file(GLOB CPP_FILES "src/*.cpp")

add_executable(llst ${CPP_FILES})
set_target_properties(llst PROPERTIES COMPILE_FLAGS "${COMPILER_FLAGS}")
set_target_properties(llst PROPERTIES LINK_FLAGS "${LINKER_FLAGS}")
