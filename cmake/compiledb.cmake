set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

execute_process(
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  # CMAKE_EXPORT_COMPILE_COMMANDS requires following to add "-std=c++17" at
  # "command" property in "compile_commands.json"
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()
