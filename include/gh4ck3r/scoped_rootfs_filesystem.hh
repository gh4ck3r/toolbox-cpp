#pragma once
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>
#include <sys/mount.h>
#include <gtest/gtest.h>
#include <gh4ck3r/file.hh>
#include <gh4ck3r/singleton.hh>

namespace gh4ck3r::filesystem::scoped_rootfs::detail {

class FilesystemContext : ThreadSingleton<FilesystemContext> {
 public:
  explicit FilesystemContext(const path_t &root) :
    root_path_ {validate_root(root)},
    host_root_ {open_dir("/")},
    host_cwd_ {open_dir(".")},
    host_mnt_ns_ {open_mnt_ns()}
  {
    if (::unshare(CLONE_NEWNS) != 0)
      system_error("create scoped mount namespace");
    try {
      if (::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0)
        system_error("make scoped mounts private");

      if (!std::filesystem::create_directories(root_path_))
        throw std::invalid_argument {"root_dir must be created: " + root_path_.string()};
      std::filesystem::permissions(root_path_, std::filesystem::perms::owner_all);
    } catch (...) {
      cleanup("rollback scoped filesystem");
      throw;
    }
  }

  ~FilesystemContext() noexcept {
    cleanup("restore scoped filesystem");
  }

  FilesystemContext(const FilesystemContext &) = delete;
  FilesystemContext &operator=(const FilesystemContext &) = delete;

 protected:
  const path_t &root_path() const noexcept { return root_path_; }

  void assert_owner() const {
    if (owner() != std::this_thread::get_id())
      throw std::runtime_error {"scoped root accessed on a different thread"};
  }

  [[noreturn]]
  static void system_error(const std::string &operation, int error = errno) {
    throw std::system_error {error, std::system_category(), operation};
  }

  static unique_fd open_dir(const path_t &dir) {
    const auto fd = ::open(dir.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd == -1) system_error("open directory " + dir.string());
    return promote_fd(fd);
  }

 private:
  void remove_private_root() noexcept {
    std::error_code error;
    std::filesystem::remove_all(root_path_, error);
    if (error)
      ADD_FAILURE() << "remove scoped root " << root_path_ << ": " << error.message();
  }

  void cleanup(const char *msg) noexcept {
    if (owner() != std::this_thread::get_id()) {
      std::fprintf(stderr, "%s: scoped root destroyed on a different thread\n", msg);
      std::terminate();
    }
    if (::setns(host_mnt_ns_, CLONE_NEWNS) != 0) {
      std::fprintf(stderr, "%s: restore mount namespace: %s\n", msg, std::strerror(errno));
      std::terminate();
    }
    if (::fchdir(host_root_) != 0 || ::chroot(".") != 0 || ::fchdir(host_cwd_) != 0) {
      std::fprintf(stderr, "%s: restore root and working directory: %s\n", msg, std::strerror(errno));
      std::terminate();
    }
    remove_private_root();
  }

  static path_t validate_root(const path_t &root) {
    auto dir = std::filesystem::absolute(root).lexically_normal();
    if (std::filesystem::exists(std::filesystem::symlink_status(dir)))
      throw std::invalid_argument {"root_dir must not exist prior to creation: " + dir.string()};
    return dir;
  }

  static unique_fd promote_fd(int fd) {
    if (const int high_fd = ::fcntl(fd, F_DUPFD_CLOEXEC, 1000); high_fd != -1) {
      ::close(fd);
      return {high_fd};
    }
    return {fd};
  }

  static unique_fd open_mnt_ns() {
    const auto fd = ::open("/proc/thread-self/ns/mnt", O_RDONLY | O_CLOEXEC);
    if (fd == -1) system_error("open mount namespace");
    return promote_fd(fd);
  }

  path_t root_path_;
  unique_fd host_root_;
  unique_fd host_cwd_;
  unique_fd host_mnt_ns_;
};

} // namespace gh4ck3r::filesystem::scoped_rootfs::detail
