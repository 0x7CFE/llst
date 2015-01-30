
# C compiler.
set (CMAKE_C_FLAGS_INIT "-Wall -Wextra")
set (CMAKE_C_FLAGS_DEBUG_INIT "-ggdb3 -O0")
set (CMAKE_C_FLAGS_MINSIZEREL_INIT "-Os")
set (CMAKE_C_FLAGS_RELEASE_INIT "-O3 -DNDEBUG")
set (CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-O2 -g")

# C++ compiler.
set (CMAKE_CXX_FLAGS_INIT "-Wall -Wextra -fexceptions -frtti")
set (CMAKE_CXX_FLAGS_DEBUG_INIT "-ggdb3 -O0")
set (CMAKE_CXX_FLAGS_MINSIZEREL_INIT "-Os")
set (CMAKE_CXX_FLAGS_RELEASE_INIT "-O3 -DNDEBUG")
set (CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-O2 -g")

# Flags used by the linker.
set (CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s")

if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
    # This is a 64-bit OS
    # LLST supports only 32-bit code
    set (CMAKE_C_FLAGS_INIT "-m32 ${CMAKE_C_FLAGS_INIT}")
    set (CMAKE_CXX_FLAGS_INIT "-m32 ${CMAKE_CXX_FLAGS_INIT}")
endif()
