#pragma once
#include <unistd.h>
#include <utility>

namespace gh4ck3r::file {

class unique_fd {
 public:
  unique_fd() = delete;
  unique_fd(int fd) : fd_(fd) {}
  unique_fd(unique_fd&& uf) : fd_(std::exchange(uf.fd_, -1)) {}
  ~unique_fd() { if (fd_ != -1) ::close(fd_); }

  operator int() const { return fd_; }
  operator bool() const { return fd_ != -1; }

 private:
  int fd_;

  unique_fd(const unique_fd&) = delete;
  unique_fd& operator=(const unique_fd&) = delete;
};

} // namespace gh4ck3r::file
