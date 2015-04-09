include(GTest)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure)

macro(cxx_test pretty_name bin_name sources libs)
    add_executable(${bin_name} ${sources})
    target_link_libraries(${bin_name} ${libs} ${GTEST_BOTH_LIBRARIES})
    set_target_properties(${bin_name} PROPERTIES COMPILE_DEFINITIONS TESTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/\")
    add_test(${pretty_name} ${bin_name})
    add_dependencies(check ${bin_name})
endmacro()

include_directories(${CMAKE_SOURCE_DIR}/include ${GTEST_INCLUDE_DIR})

