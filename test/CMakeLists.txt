cmake_minimum_required(VERSION 3.20)
find_package(GTest)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  set(INSTALL_GTEST OFF)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
  string(APPEND CMAKE_CXX_FLAGS " -pipe -pedantic -Wall")
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " -ggdb3")
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -Werror")
endif()

link_libraries(gh4ck3r GTest::gtest_main)

include(GoogleTest)
function(add_unittest TestMainSrc)
  cmake_path(GET TestMainSrc STEM LAST_ONLY TESTNAME)
  add_executable(${TESTNAME} ${TestMainSrc} ${ARGN})
  gtest_discover_tests(${TESTNAME})
endfunction()

add_unittest(hexdump.test.cc)
add_unittest(lazygetter.test.cc)
add_unittest(concat.test.cc)
add_unittest(logger.test.cc)
add_unittest(recipe.test.cc)
add_unittest(base64.test.cc)
add_unittest(defer.test.cc)
