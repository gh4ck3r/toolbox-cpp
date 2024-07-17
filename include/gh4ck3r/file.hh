#pragma once
#include <unistd.h>
#include <utility>

namespace gh4ck3r::file {

class unique_fd {
 public:
  unique_fd(int fd) : fd_(fd) {}
  unique_fd(unique_fd&& uf) : fd_(std::exchange(uf.fd_, uninitialied)) {}
  ~unique_fd() { if (fd_ != uninitialied) { ::close(fd_); } }

  unique_fd() = delete;
  unique_fd(unique_fd&) = delete;
  unique_fd(const unique_fd&) = delete;
  unique_fd& operator=(const unique_fd&) = delete;

  inline operator int() const { return fd_; }
  inline operator bool() const { return fd_ != uninitialied; }

 private:
  int fd_;

  static inline constexpr int uninitialied = -1;
};

} // namespace gh4ck3r::file
