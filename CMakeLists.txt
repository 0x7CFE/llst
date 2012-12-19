cmake_minimum_required(VERSION 2.8)

project(llst)

add_subdirectory(src)
add_subdirectory(include)

file(GLOB CPP_FILES "src/*.cpp")

add_executable(llst ${CPP_FILES})

set_target_properties(llst PROPERTIES COMPILE_FLAGS "-g -m32 -Wall")
set_target_properties(llst PROPERTIES LINK_FLAGS "-m32 -L/usr/lib32 -Wall")

include_directories(include)

