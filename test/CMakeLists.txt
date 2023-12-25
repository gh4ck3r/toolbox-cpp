cmake_minimum_required(VERSION 3.20)
include(GoogleTest)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(INSTALL_GTEST OFF)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
add_compile_options(
  -Wall
  -Wextra
  $<$<CONFIG:Release>:-Werror>
  -pedantic
  -g3
  -ggdb3
)
endif()

link_libraries(gh4ck3r gtest_main)

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
