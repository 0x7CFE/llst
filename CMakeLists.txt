cmake_minimum_required(VERSION 2.8.4)

set (CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_SOURCE_DIR}/cmake/variables.cmake")
set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(llst-project)

find_package(Threads REQUIRED QUIET)
find_package(READLINE)
find_package(TINFO)
find_package(LLVM 3.1 EXACT)
find_package(POD2MAN)
find_package(GZIP REQUIRED)

option(USE_READLINE "Should we use the GNU readline and history libraries?" ON)
option(USE_LLVM "Should we use LLVM to build JIT?" OFF)
option(USE_POD2MAN "Should we use pod2man to build the documentation (we will create empty docs otherwise)?" ON)

if (USE_LLVM)
    if (LLVM_FOUND)
        message(STATUS "Using LLVM ${LLVM_VERSION}")
        set (CMAKE_C_FLAGS "${LLVM_C_FLAGS} ${CMAKE_C_FLAGS}")
        set (CMAKE_CXX_FLAGS "${LLVM_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")
        set (CMAKE_EXE_LINKER_FLAGS "${LLVM_LD_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")

        # LLVM generates loads of warnings...
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-qual -Wno-unused-parameter")

        set (LLVM_LIBS_TO_LINK ${LLVM_LIBS})
        add_definitions(-DLLVM)
    else()
        message(FATAL_ERROR "\nInstall llvm-3.1-dev:i386 and try again.")
    endif()
else()
    message(STATUS "LLVM is disabled")
    unset(LLVM_LIBS_TO_LINK)
endif()

if (USE_READLINE)
    if (READLINE_FOUND AND TINFO_FOUND)
        message(STATUS "Using readline library")
        set_source_files_properties(src/CompletionEngine.cpp PROPERTIES COMPILE_DEFINITIONS USE_READLINE)
        set (READLINE_LIBS_TO_LINK ${READLINE_LIBRARIES} ${TINFO_LIBRARIES})
    else()
        if(NOT TINFO_FOUND)
            message(SEND_ERROR "Library readline depends on tinfo.\nYou may configure with -DUSE_READLINE=OFF.")
        endif()
        if(NOT READLINE_FOUND)
            message(SEND_ERROR "Library readline is not found.\nYou may configure with -DUSE_READLINE=OFF.")
        endif()
    endif()
else()
    message(STATUS "Readline library is disabled")
    unset(READLINE_LIBS_TO_LINK)
endif()

if (USE_POD2MAN)
    if (POD2MAN_FOUND)
        message(STATUS "Using pod2man to build the documentation")
    else()
        message(FATAL_ERROR "\npod2man is not found.\nYou may configure with -DUSE_POD2MAN=OFF.")
    endif()
    function(get_pod2man_cmd OUT)
        get_pod2man_cmd_wrapper(CMD ${ARGN})
        set("${OUT}" "${CMD}" PARENT_SCOPE)
    endfunction()
else()
    message(STATUS "Pod2man is disabled")
    function(get_pod2man_cmd OUT)
        get_pod2man_cmd_wrapper(CMD ${ARGN} STUB)
        set("${OUT}" "${CMD}" PARENT_SCOPE)
    endfunction()
endif()


add_subdirectory(doc)
add_subdirectory(image)
add_subdirectory(src)
add_subdirectory(include)
include_directories(include)

# Base set of sources needed in every build
set(CPP_FILES
    src/args.cpp
    src/BakerMemoryManager.cpp
    src/CompletionEngine.cpp
    src/GenerationalMemoryManager.cpp
    src/Image.cpp
    src/main.cpp
    src/NonCollectMemoryManager.cpp
    src/primitives.cpp
    src/TDictionary.cpp
    src/TInstruction.cpp
    src/TSymbol.cpp
    src/vm.cpp
)

if (USE_LLVM)
    # LLVM specific sources
    list(APPEND CPP_FILES
        src/LLVMMemoryManager.cpp
        src/MethodCompiler.cpp
        src/JITRuntime.cpp
        src/llstPass.cpp
        src/llstDebuggingPass.cpp
    )
endif()

add_executable(llst ${CPP_FILES})
target_link_libraries(llst ${LLVM_LIBS_TO_LINK} ${READLINE_LIBS_TO_LINK} ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS})
add_dependencies(llst image)

set(changelog_compressed "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz")
gzip_compress("compress_changelog" "${CMAKE_CURRENT_SOURCE_DIR}/ChangeLog" ${changelog_compressed})

install(TARGETS llst DESTINATION bin)
install(FILES LICENSE DESTINATION share/doc/${PROJECT_NAME} RENAME copyright)
install(FILES ${changelog_compressed} DESTINATION share/doc/${PROJECT_NAME})
install(FILES misc/bash-completion.sh DESTINATION share/bash-completion/completions RENAME llst)


set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "LLVM powered Little Smalltalk")
set (CPACK_DEBIAN_PACKAGE_DESCRIPTION # The format of Description: http://www.debian.org/doc/debian-policy/ch-controlfields.html#s-f-Description
"LLVM powered Little Smalltalk
 LLST is a virtual machine with integrated JIT.
")
set (CPACK_PACKAGE_CONTACT "info@llst.org")
set (CPACK_PACKAGE_VERSION_MAJOR "0")
set (CPACK_PACKAGE_VERSION_MINOR "2")
set (CPACK_PACKAGE_VERSION_PATCH "1")
set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set (CPACK_SOURCE_GENERATOR "TGZ")
set (CPACK_SOURCE_IGNORE_FILES "build/" "\\\\.kdev.*" "\\\\.git.*" "\\\\.hgignore" ".*\\\\.image")
set (CPACK_STRIP_FILES "llst")


set (CPACK_GENERATOR "DEB")
set (CPACK_DEBIAN_PACKAGE_MAINTAINER "Team <info@llst.org>")
set (CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libreadline6-dev")

include(CPack)

# uninstall target
configure_file( "${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake.in" uninstall.cmake @ONLY)
add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P uninstall.cmake)
