include(FetchContent)
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
)
message(CHECK_START "Downloading asio")
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
  FetchContent_Populate(asio)
endif()

if(EXISTS "${asio_SOURCE_DIR}/asio/include")
  add_library(asio::asio INTERFACE IMPORTED GLOBAL)
  target_include_directories(asio::asio INTERFACE "${asio_SOURCE_DIR}/asio/include")
  target_compile_definitions(asio::asio INTERFACE ASIO_STANDALONE)
  message(CHECK_PASS "done")
else()
  message(CHECK_FAIL "failed")
endif()
