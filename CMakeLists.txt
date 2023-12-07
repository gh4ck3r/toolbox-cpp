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

enable_testing()
add_subdirectory(test)
