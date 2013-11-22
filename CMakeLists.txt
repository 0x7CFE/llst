cmake_minimum_required(VERSION 2.8)

project(llst_project)

if (LLVM)
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

    add_definitions(-DLLVM)
endif()

set (CMAKE_CXX_FLAGS "${LLVM_CXX_FLAGS} -Wall -fexceptions -frtti -Wno-cast-qual")
set (CMAKE_CXX_FLAGS_DEBUG "-g -ggdb -O0")
set (CMAKE_CXX_FLAGS_RELEASE "-O3")

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

if (LLVM)
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
target_link_libraries(llst ${LLVM_LIBS} readline ${LLVM_LD_FLAGS})

if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
    # this is a 64-bit OS
    add_definitions(-m32 -L/usr/lib32)
    set(CMAKE_EXE_LINKER_FLAGS "-m32 -L/usr/lib32")
endif()

set (IMAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/image)
add_custom_command(
    OUTPUT  ${IMAGE_DIR}/LittleSmalltalk.image
    COMMAND ${IMAGE_DIR}/imageBuilder
    DEPENDS ${IMAGE_DIR}/imageSource.st
    WORKING_DIRECTORY ${IMAGE_DIR}
    COMMENT "Building image"
)

add_custom_target(image ALL DEPENDS ${IMAGE_DIR}/LittleSmalltalk.image)

install(TARGETS llst DESTINATION bin)

set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "LLVM powered Little Smalltalk")
set (CPACK_PACKAGE_CONTACT "info@llst.org")
set (CPACK_PACKAGE_VERSION_MAJOR "0")
set (CPACK_PACKAGE_VERSION_MINOR "2")
set (CPACK_PACKAGE_VERSION_PATCH "0")
set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set (CPACK_SOURCE_GENERATOR "TGZ")
set (CPACK_SOURCE_IGNORE_FILES "build/;\\\\.kdev4.*;\\\\.git/;\\\\.hgignore")
set (CPACK_GENERATOR "DEB")

include(CPack)
