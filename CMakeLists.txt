cmake_minimum_required(VERSION 3.20)
project(gh4ck3r-snippets
  VERSION 0.0.1
  DESCRIPTION "collection of snippets for C++17"
  HOMEPAGE_URL https://github.com/gh4ck3r/cpp-snippets
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
execute_process(
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
)

add_library(gh4ck3r INTERFACE)
target_include_directories(gh4ck3r INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(gh4ck3r INTERFACE cxx_std_17)
set_target_properties(gh4ck3r PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
  DEBUG_POSTFIX d
)

install(TARGETS gh4ck3r)
install(
  DIRECTORY include/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

enable_testing()
add_subdirectory(test)
