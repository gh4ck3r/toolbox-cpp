cmake_minimum_required(VERSION 3.20)
project(toolbox-cpp
  VERSION 0.0.1
  DESCRIPTION "collection of snippets for C++17"
  HOMEPAGE_URL https://github.com/gh4ck3r/toolbox-cpp
  LANGUAGES CXX
  )

option(GH4CK3R_USE_HTTP "use http server/client; It depends other projects")

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(compiledb)

# use package from host first
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

add_library(gh4ck3r INTERFACE
  include/gh4ck3r/base64.hh
  include/gh4ck3r/concat.hh
  include/gh4ck3r/defer.hh
  include/gh4ck3r/hexdump.hh
  include/gh4ck3r/lazygetter.hh
  include/gh4ck3r/logger.hh
  include/gh4ck3r/recipe.hh
  include/gh4ck3r/typemap.hh
  )

target_compile_features(gh4ck3r INTERFACE cxx_std_17)

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

get_target_property(PUBLIC_HEADER_FILES gh4ck3r SOURCES)
set_target_properties(gh4ck3r PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
  PUBLIC_HEADER "${PUBLIC_HEADER_FILES}"
  DEBUG_POSTFIX d
  )

include(GNUInstallDirs)
install(TARGETS gh4ck3r
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gh4ck3r
  )

enable_testing()
add_subdirectory(test)
