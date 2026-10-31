#pragma once
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
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
    try {
      prepare();
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

  const path_t &root_path() const noexcept { return root_path_; }

  void enter() {
    require_preparing();
    if (::chroot(root_path_.c_str()) != 0)
      system_error("enter scoped root");
    root_entered_ = true;
    preparing_ = false;
    if (::chdir("/") != 0) system_error("enter scoped working directory");
  }

  void leave() {
    assert_owner();
    if (!namespace_active_) return;
    // pthread join can precede the kernel's final fs_struct release.
    if (::unshare(CLONE_FS) != 0)
      system_error("isolate filesystem state before restoration");
    if (root_entered_) {
      if (::fchdir(*private_root_) != 0 || ::chroot(".") != 0)
        system_error("leave scoped root");
      root_entered_ = false;
    }
    preparing_ = false;
  }

  void assert_owner() const {
    if (owner() != std::this_thread::get_id())
      throw std::runtime_error {"scoped root destroyed on a different thread"};
  }

  void require_preparing() const {
    assert_owner();
    if (!preparing_)
      throw std::logic_error {"scoped root is not being prepared"};
  }

  void require_mount_access() const {
    assert_owner();
    if (!namespace_active_ || root_entered_)
      throw std::logic_error {"scoped mounts require the private namespace outside chroot"};
  }

 private:
  void prepare() {
    assert_owner();
    if (namespace_active_)
      throw std::logic_error {"scoped mount namespace already prepared"};
    if (::unshare(CLONE_NEWNS) != 0)
      system_error("create scoped mount namespace");

    namespace_active_ = true;
    private_root_.emplace(open_dir("/"));
    if (::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0)
      system_error("make scoped mounts private");

    if (!std::filesystem::create_directories(root_path_))
      throw std::invalid_argument {"root_dir must be created: " + root_path_.string()};
    root_owned_ = true;
    std::filesystem::permissions(root_path_, std::filesystem::perms::owner_all);
    preparing_ = true;
  }

  void restore() {
    assert_owner();
    if (!namespace_active_) return;
    require_mount_access();
    if (::setns(host_mnt_ns_, CLONE_NEWNS) != 0)
      system_error("restore mount namespace");
    if (::fchdir(host_root_) != 0 || ::chroot(".") != 0 || ::fchdir(host_cwd_) != 0)
      system_error("restore root and working directory");
    private_root_.reset();
    namespace_active_ = false;
    preparing_ = false;
  }

  void remove_private_root() noexcept {
    if (!root_owned_) return;
    std::error_code error;
    std::filesystem::remove_all(root_path_, error);
    if (!error)
      root_owned_ = false;
    else
      ADD_FAILURE() << "remove scoped root " << root_path_ << ": " << error.message();
  }

  void cleanup(const char *msg) noexcept {
    try {
      restore();
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s: %s\n", msg, error.what());
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

  [[noreturn]]
  static void system_error(const std::string &operation, int error = errno) {
    throw std::system_error {error, std::system_category(), operation};
  }

  static unique_fd promote_fd(int fd) {
    if (const int high_fd = ::fcntl(fd, F_DUPFD_CLOEXEC, 1000); high_fd != -1) {
      ::close(fd);
      return {high_fd};
    }
    return {fd};
  }

  static unique_fd open_dir(const path_t &dir) {
    const auto fd = ::open(dir.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd == -1) system_error("open directory " + dir.string());
    return promote_fd(fd);
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
  std::optional<unique_fd> private_root_;
  bool namespace_active_ = false;
  bool root_owned_ = false;
  bool root_entered_ = false;
  bool preparing_ = false;
};

} // namespace gh4ck3r::filesystem::scoped_rootfs::detail
