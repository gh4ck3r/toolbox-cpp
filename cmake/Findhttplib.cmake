include(FetchContent)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.14.3
)
set(HTTPLIB_INSTALL OFF)
message(CHECK_START "Downloading httplib")
FetchContent_MakeAvailable(httplib)
if (TARGET httplib::httplib)
  message(CHECK_PASS "done")
else()
  message(CHECK_FAIL "failed")
endif()
