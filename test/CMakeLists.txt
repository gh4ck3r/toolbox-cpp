cmake_minimum_required(VERSION 3.20)
find_package(GTest)

if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
  string(APPEND CMAKE_CXX_FLAGS " -pipe -pedantic -Wall")
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " -ggdb3")
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -Werror")
endif()

link_libraries(gh4ck3r GTest::gtest_main)

include(GoogleTest)
function(add_unittest TestMainSrc)
  cmake_path(GET TestMainSrc STEM LAST_ONLY TESTNAME)
  list(JOIN ARGV " " SRCS)
  add_executable(${TESTNAME} ${SRCS})
  gtest_discover_tests(${TESTNAME})
endfunction()

add_unittest(hexdump.test.cc)
add_unittest(lazygetter.test.cc)
add_unittest(concat.test.cc)
add_unittest(logger.test.cc)
add_unittest(recipe.test.cc)
add_unittest(base64.test.cc)
add_unittest(defer.test.cc)
add_unittest(type_traits.test.cc)
add_unittest(typemap.test.cc)

if (TARGET httplib::httplib)
  add_unittest(http.test.cc)
endif()
