include(GTest)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure)

macro(cxx_test pretty_name bin_name sources libs)
    add_executable(${bin_name} ${sources})
    target_link_libraries(${bin_name} supc++ -pthread ${libs} ${GTEST_BOTH_LIBRARIES} ${READLINE_LIBS_TO_LINK})
    set_target_properties(${bin_name} PROPERTIES COMPILE_DEFINITIONS TESTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/\")
    add_test(${pretty_name} ${bin_name})
    add_dependencies(check ${bin_name})
endmacro()

include_directories(${CMAKE_SOURCE_DIR}/include ${GTEST_INCLUDE_DIR})

add_subdirectory(radix_tree)

cxx_test(TInteger test_tinteger "${CMAKE_CURRENT_SOURCE_DIR}/tinteger.cpp" "")
cxx_test(TSmalltalkInstruction test_tsmalltalk_instruction "${CMAKE_CURRENT_SOURCE_DIR}/tsmalltalk_instruction.cpp" "stapi;standard_set")
cxx_test(InstructionDecoder test_instruction_decoder "${CMAKE_CURRENT_SOURCE_DIR}/instruction_decoder.cpp" "stapi;standard_set")
cxx_test(ControlGraph test_control_graph "${CMAKE_CURRENT_SOURCE_DIR}/control_graph.cpp" "stapi;standard_set")
cxx_test(ABABProblem test_abab_problem "${CMAKE_CURRENT_SOURCE_DIR}/abab_problem.cpp" "stapi;standard_set")
cxx_test(StackSemantics test_stack_semantics "${CMAKE_CURRENT_SOURCE_DIR}/stack_semantics.cpp" "stapi;standard_set")
# TODO cxx_test(StackUnderflow test_stack_underflow "${CMAKE_CURRENT_SOURCE_DIR}/stack_underflow.cpp" "stapi")
cxx_test(DecodeAllMethods test_decode_all_methods "${CMAKE_CURRENT_SOURCE_DIR}/decode_all_methods.cpp" "stapi;memory_managers;standard_set")
cxx_test("VM::primitives" test_vm_primitives "${CMAKE_CURRENT_SOURCE_DIR}/vm_primitives.cpp" "memory_managers;standard_set")
