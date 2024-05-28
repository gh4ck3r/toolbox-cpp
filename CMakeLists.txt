cmake_minimum_required(VERSION 3.20)
project(gh4ck3r-snippets
  VERSION 0.0.1
  DESCRIPTION "collection of snippets for C++17"
  HOMEPAGE_URL https://github.com/gh4ck3r/cpp-snippets
  LANGUAGES CXX
  )

option(GH4CK3R_USE_HTTP "use http server/client; It depends other projects")

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(compiledb)

# use package from host first
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

add_library(gh4ck3r INTERFACE)

target_include_directories(gh4ck3r INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

if(GH4CK3R_USE_HTTP)
  find_package(httplib REQUIRED)
else()
  find_package(httplib QUIET)
endif()

if (TARGET httplib::httplib)
  target_include_directories(gh4ck3r
    INTERFACE
      $<$<BOOL:${httplib_FOUND}>:${HTTPLIB_INCLUDE_DIR}>
    )
  target_link_libraries(gh4ck3r INTERFACE httplib::httplib)
endif()

target_compile_features(gh4ck3r INTERFACE cxx_std_17)
set_target_properties(gh4ck3r PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
  DEBUG_POSTFIX d
  )

include(GNUInstallDirs)
install(TARGETS gh4ck3r)
install(
  DIRECTORY include/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

enable_testing()
add_subdirectory(test)
