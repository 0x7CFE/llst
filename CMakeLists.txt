cmake_minimum_required(VERSION 2.8)

# A convenience variable:
set(LLVM_ROOT "/usr/lib/llvm-3.1" CACHE PATH "Root of LLVM install.")

# A bit of a sanity check:
if( NOT EXISTS ${LLVM_ROOT}/include/llvm )
message(FATAL_ERROR "LLVM_ROOT (${LLVM_ROOT}) is not a valid LLVM install")
endif()

# We incorporate the CMake features provided by LLVM:
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_ROOT}/share/llvm/cmake")
include(LLVMConfig)

# Now set the header and library paths:
include_directories( ${LLVM_INCLUDE_DIRS} )
link_directories( ${LLVM_LIBRARY_DIRS} )
add_definitions( ${LLVM_DEFINITIONS} )

# Let's suppose we want to build a JIT compiler with support for
# binary code (no interpreter):
llvm_map_components_to_libraries(REQ_LLVM_LIBRARIES jit native)


set (COMPILER_FLAGS "-O3 -m32 -march=native -mtune=native -Ofast -flto -funroll-loops -Wall ${LLVM_CXX_FLAGS}")
set (LINKER_FLAGS "-m32 -flto -L/usr/lib32 -Wall ${LLVM_LD_FLAGS}")

project(llst)

add_subdirectory(src)
add_subdirectory(include)

file(GLOB CPP_FILES "src/*.cpp")

add_executable(llst ${CPP_FILES})
set_target_properties(llst PROPERTIES COMPILE_FLAGS "${COMPILER_FLAGS}")
set_target_properties(llst PROPERTIES LINK_FLAGS "${LINKER_FLAGS}")
target_link_libraries(llst ${REQ_LLVM_LIBRARIES})

include_directories(include)

