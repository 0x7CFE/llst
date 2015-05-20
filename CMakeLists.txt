cmake_minimum_required(VERSION 2.8.4)

set (CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_SOURCE_DIR}/cmake/variables.cmake")
set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set (LLVM_PACKAGE_VERSION 3.3)

project(llst)

find_package(Threads REQUIRED QUIET)
find_package(READLINE)
find_package(TINFO)
find_package(LLVM ${LLVM_PACKAGE_VERSION} EXACT)
find_package(POD2MAN)
find_package(GZIP REQUIRED)

option(USE_READLINE "Should we use the GNU readline and history libraries?" ON)
option(USE_LLVM "Should we use LLVM to build JIT?" OFF)
option(USE_POD2MAN "Should we use pod2man to build the documentation (we will create empty docs otherwise)?" ON)

if (USE_LLVM)
    if (LLVM_FOUND)
        message(STATUS "Using LLVM ${LLVM_VERSION}")
        # LLVM generates loads of warnings...
        set (CMAKE_CXX_FLAGS "${LLVM_CPP_FLAGS} ${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
        set (LLVM_LIBS_TO_LINK ${LLVM_LIBS})
        link_directories(${LLVM_LIB_DIR})
        add_definitions(-DLLVM)
    else()
        message(FATAL_ERROR "\nInstall llvm-${LLVM_PACKAGE_VERSION}-dev:i386 and try again.")
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


option(BUILD_TESTS "Should we build tests?" OFF)

if (BUILD_TESTS)
    enable_testing()
    configure_file( "${CMAKE_SOURCE_DIR}/cmake/CTestCustom.cmake.in" "${CMAKE_BINARY_DIR}/CTestCustom.cmake" @ONLY)
    include(CTest)
    add_subdirectory(tests)
endif()

add_subdirectory(doc)
add_subdirectory(image)
add_subdirectory(src)
add_subdirectory(include)
include_directories(include)

add_library(stapi
    src/TSmalltalkInstruction.cpp
    src/InstructionDecoder.cpp
    src/ParsedBytecode.cpp
    src/ParsedMethod.cpp
    src/ParsedBlock.cpp

    src/ControlGraph.cpp
    src/ControlGraphVisualizer.cpp
)

set(MM_CPP_FILES
    src/BakerMemoryManager.cpp
    src/GenerationalMemoryManager.cpp
    src/NonCollectMemoryManager.cpp
)
if (USE_LLVM)
    list(APPEND MM_CPP_FILES src/LLVMMemoryManager.cpp)
endif()

add_library(memory_managers ${MM_CPP_FILES})

# Base set of sources needed in every build
add_library(standart_set
    src/args.cpp
    src/CompletionEngine.cpp
    src/Image.cpp
    src/primitives.cpp
    src/TDictionary.cpp
    src/TSymbol.cpp
    src/vm.cpp

    src/TSmalltalkInstruction.cpp
    src/InstructionDecoder.cpp
    src/ParsedMethod.cpp
    src/ParsedBytecode.cpp
    src/ParsedBlock.cpp

    src/ControlGraph.cpp
    src/ControlGraphVisualizer.cpp
    src/Timer.cpp
    src/GCLogger.cpp
)

if (USE_LLVM)
    add_library(jit
        src/MethodCompiler.cpp
        src/JITRuntime.cpp
        src/llstPass.cpp
        src/llstDebuggingPass.cpp
    )
endif()

add_executable(llst src/main.cpp src/vm.cpp)
target_link_libraries(llst standart_set memory_managers stapi ${READLINE_LIBS_TO_LINK} ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS})
add_dependencies(llst image)

if (USE_LLVM)
    target_link_libraries(llst jit ${LLVM_LIBS} ${LLVM_LD_FLAGS})
endif()

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
set (CPACK_PACKAGE_VERSION_MINOR "3")
set (CPACK_PACKAGE_VERSION_PATCH "0")
set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set (CPACK_SOURCE_GENERATOR "TGZ")
set (CPACK_SOURCE_IGNORE_FILES "build/" "\\\\.kdev.*" "\\\\.git.*" "\\\\.hgignore" ".*\\\\.image")
set (CPACK_STRIP_FILES "llst")


set (CPACK_GENERATOR "DEB")
set (CPACK_DEBIAN_PACKAGE_HOMEPAGE "http://llst.org")
set (CPACK_DEBIAN_PACKAGE_MAINTAINER "Team <info@llst.org>")
set (CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
set (CPACK_DEBIAN_PACKAGE_DEPENDS "libc6:i386, libgcc1:i386, libstdc++6:i386, libreadline6:i386, libtinfo5:i386")

include(CPack)

# uninstall target
configure_file( "${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake.in" uninstall.cmake @ONLY)
add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P uninstall.cmake)
