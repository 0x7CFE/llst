
find_package(GTest REQUIRED)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure)

macro(cxx_test pretty_name bin_name sources libs)
    add_executable(${bin_name} EXCLUDE_FROM_ALL ${sources})
    if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
        # this is a 64-bit OS
        set_target_properties(${bin_name} PROPERTIES
            COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
    endif()
    set_target_properties(${bin_name} PROPERTIES COMPILE_DEFINITIONS TESTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/\")
    target_link_libraries(${bin_name} ${libs} ${GTEST_BOTH_LIBRARIES} )
    add_test(${pretty_name} ${bin_name})
    add_dependencies(check ${bin_name})
endmacro()

include_directories(${CMAKE_SOURCE_DIR}/include)
