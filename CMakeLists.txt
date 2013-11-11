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

set (COMPILER_FLAGS "-m32 -march=native -mtune=native -funroll-loops -Wall ${LLVM_CXX_FLAGS} -O2 -fexceptions -frtti -Wno-cast-qual")
set (LINKER_FLAGS   "-m32 -L/usr/lib32 -Wall ${LLVM_LD_FLAGS}")

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
target_link_libraries(llst ${LLVM_LIBS} -ldl -lpthread -lreadline)
set_target_properties(llst PROPERTIES COMPILE_FLAGS "${COMPILER_FLAGS}")
set_target_properties(llst PROPERTIES LINK_FLAGS "${LINKER_FLAGS}")

set (IMAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/image)
add_custom_command(
    OUTPUT  ${IMAGE_DIR}/LittleSmalltalk.image
    COMMAND ${IMAGE_DIR}/imageBuilder
    DEPENDS ${IMAGE_DIR}/imageSource.st
    WORKING_DIRECTORY ${IMAGE_DIR}
    COMMENT "Building image"
)

add_custom_target(image ALL DEPENDS ${IMAGE_DIR}/LittleSmalltalk.image)
