include_directories(${CMAKE_SOURCE_DIR}/include/radix_tree)

cxx_test("radix_tree::insert" test_radix_tree_insert "test_radix_tree_insert.cpp" "")
cxx_test("radix_tree::erase" test_radix_tree_erase "test_radix_tree_erase.cpp" "")
cxx_test("radix_tree::find" test_radix_tree_find "test_radix_tree_find.cpp" "")
cxx_test("radix_tree::prefix_match" test_radix_tree_prefix_match "test_radix_tree_prefix_match.cpp" "")
cxx_test("radix_tree::longest_match" test_radix_tree_longest_match "test_radix_tree_longest_match.cpp" "")
cxx_test("radix_tree::greedy_match" test_radix_tree_greedy_match "test_radix_tree_greedy_match.cpp" "")
cxx_test("radix_tree_iterator" test_radix_tree_iterator "test_radix_tree_iterator.cpp" "")
