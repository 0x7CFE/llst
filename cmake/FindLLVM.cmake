
# Find LLVM (a collection of libraries and tools that make it easy to build compilers,
#  optimizers, Just-In-Time code generators, and many other compiler-related programs)
# Export variables:
#  LLVM_FOUND
#  LLVM_CONFIG_EXE
#  LLVM_CPP_FLAGS
#  LLVM_CXX_FLAGS
#  LLVM_C_FLAGS
#  LLVM_LD_FLAGS
#  LLVM_INSTALL_PREFIX
#  LLVM_VERSION
#  LLVM_LIBS
#  LLVM_LIBFILES
#  LLVM_INCLUDE_DIR
#  LLVM_LIB_DIR

include(CheckIncludeFileCXX)
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
include(FindPackageHandleStandardArgs)

if(LLVM_FOUND AND PREVIOUS_FOUND_VERSION EQUAL LLVM_FIND_VERSION)
    # No need to find it again.
    return()
else()
    unset(LLVM_FOUND CACHE)
    unset(LLVM_PREVIOUS_FOUND_VERSION CACHE)
    unset(LLVM_CONFIG_EXE CACHE)
    unset(LLVM_LIBS_INSTALLED CACHE)
    unset(LLVM_HEADERS_INSTALLED CACHE)
    unset(LLVM_COMPILED_AND_LINKED CACHE)
endif()

message(STATUS "Looking for LLVM ${LLVM_FIND_VERSION}")

function(get_llvm_config_var args out_var)
    unset(${out_var} CACHE)
    if (NOT LLVM_CONFIG_EXE)
        return()
    endif()
    execute_process(
        COMMAND ${LLVM_CONFIG_EXE} ${args}
        OUTPUT_VARIABLE ${out_var}
        RESULT_VARIABLE exit_code
        ERROR_VARIABLE std_err_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (exit_code)
        message(SEND_ERROR "Executing '${LLVM_CONFIG_EXE} ${args}' exited with '${exit_code}'")
        message(FATAL_ERROR "Error message: ${std_err_output}")
    else()
        set(${out_var} "${${out_var}}" CACHE INTERNAL "${out_var}")
    endif()
endfunction()

macro(check_llvm_libs out_var)
    if (${out_var})
        return() # Result is cached
    endif()

    set (libs_exist YES)

    string(REPLACE " " ";" LIBS_LIST "${LLVM_LIBFILES}")
    if (NOT LIBS_LIST)
        set (libs_exist NOTFOUND)
    endif()

    foreach(lib ${LIBS_LIST})
        if (NOT EXISTS ${lib})
            message(STATUS "File ${lib} is missing")
            set (libs_exist NOTFOUND)
            break()
        endif()
    endforeach()

    set (${out_var} "${libs_exist}" CACHE INTERNAL "")
endmacro()

macro(check_llvm_header header out_var)
    CMAKE_PUSH_CHECK_STATE()
    set(CMAKE_REQUIRED_FLAGS "${LLVM_CXX_FLAGS}")
    CHECK_INCLUDE_FILE_CXX("${header}" ${out_var})
    CMAKE_POP_CHECK_STATE()
endmacro()

macro(check_llvm_source_compiles code out_var)
    CMAKE_PUSH_CHECK_STATE()
    set(CMAKE_REQUIRED_FLAGS ${LLVM_CXX_FLAGS})
    set(CMAKE_REQUIRED_LIBRARIES ${LLVM_LIBS} ${LLVM_LD_FLAGS})
    CHECK_CXX_SOURCE_COMPILES("${code}" ${out_var})
    CMAKE_POP_CHECK_STATE()
endmacro()

set(LLVM_CONFIG_NAMES "llvm-config-${LLVM_FIND_VERSION}" llvm-config)
if (NOT LLVM_FIND_VERSION_EXACT)
    foreach(version 3.1 3.2 3.3 3.4 3.5)
        list(APPEND LLVM_CONFIG_NAMES "llvm-config-${version}")
    endforeach()
endif()

find_program(LLVM_CONFIG_EXE NAMES ${LLVM_CONFIG_NAMES} DOC "Full path to llvm-config")
mark_as_advanced(LLVM_CONFIG_EXE)
if (NOT LLVM_CONFIG_EXE)
    message(STATUS "Could NOT find llvm-config (tried ${LLVM_CONFIG_NAMES})")
endif()

get_llvm_config_var(--cppflags   LLVM_CPP_FLAGS)
get_llvm_config_var(--cxxflags   LLVM_CXX_FLAGS)
get_llvm_config_var(--cflags     LLVM_C_FLAGS)
get_llvm_config_var(--ldflags    LLVM_LD_FLAGS)
get_llvm_config_var(--prefix     LLVM_INSTALL_PREFIX)
get_llvm_config_var(--version    LLVM_VERSION)
get_llvm_config_var(--libs       LLVM_LIBS)
get_llvm_config_var(--libfiles   LLVM_LIBFILES)
get_llvm_config_var(--includedir LLVM_INCLUDE_DIR)
get_llvm_config_var(--libdir     LLVM_LIB_DIR)

check_llvm_libs(LLVM_LIBS_INSTALLED)
check_llvm_header("llvm/Support/TargetSelect.h" LLVM_HEADERS_INSTALLED)
check_llvm_source_compiles("#include <llvm/Support/TargetSelect.h> \n int main(){ return 0; }" LLVM_COMPILED_AND_LINKED)

if (LLVM_HEADERS_INSTALLED AND NOT LLVM_LIBS_INSTALLED)
    message(STATUS "Only header files installed in the package")
elseif (LLVM_LIBS_INSTALLED AND NOT LLVM_HEADERS_INSTALLED)
    message(STATUS "Libs installed while header files are missing")
elseif (LLVM_HEADERS_INSTALLED AND LLVM_LIBS_INSTALLED AND NOT LLVM_COMPILED_AND_LINKED)
    message(STATUS "Libs and headers are installed, but during test compilation something went wrong. See ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log for details")
endif()

FIND_PACKAGE_HANDLE_STANDARD_ARGS( LLVM
    FOUND_VAR LLVM_FOUND
    REQUIRED_VARS LLVM_CONFIG_EXE LLVM_LIBS_INSTALLED LLVM_HEADERS_INSTALLED LLVM_COMPILED_AND_LINKED
    VERSION_VAR LLVM_VERSION
)

set(LLVM_FOUND ${LLVM_FOUND} CACHE INTERNAL "LLVM_FOUND")
if (LLVM_FOUND)
    set(PREVIOUS_FOUND_VERSION ${LLVM_VERSION} CACHE INTERNAL "The version of LLVM found by previous call find_package")
endif()
