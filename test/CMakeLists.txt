include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
set(INSTALL_GTEST "Enable installation of googletest." OFF)

add_executable(hexdump.test hexdump.test.cc)
target_compile_features(hexdump.test PRIVATE cxx_std_17)
target_include_directories(hexdump.test PRIVATE ../include)
target_link_libraries(hexdump.test PRIVATE gtest_main)
gtest_discover_tests(hexdump.test)
