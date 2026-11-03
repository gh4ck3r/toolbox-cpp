#pragma once
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <gmock/gmock.h>
#include "scoped_rootfs_filesystem.hh"
#include "scoped_rootfs_mock_environment.hh"

namespace gh4ck3r::filesystem::scoped_rootfs {

class ScopedRootFsBuilder;

// Thread-bound, non-copyable guard. Binds are non-recursive and read-only.
// Commands must have completed before destruction; additional mounts must be
// released by their owner before this guard restores the previous filesystem.
class ScopedRootFs : detail::FilesystemContext, detail::MockEnvironment {
 public:
  ScopedRootFs(const ScopedRootFs &) = delete;
  ScopedRootFs &operator=(const ScopedRootFs &) = delete;
  ScopedRootFs(ScopedRootFs &&) = delete;
  ScopedRootFs &operator=(ScopedRootFs &&) = delete;

  ~ScopedRootFs() noexcept {
    cleanup("restore scoped root");
    report_worker_error();
  }

  using MockEnvironment::mock;

 private:
  friend class ScopedRootFsBuilder;
  using FilesystemContext::system_error;
  using MockEnvironment::Binding;
  using MockEntry = MockEnvironment::Entry;

  ScopedRootFs(const path_t &root,
               const std::vector<Binding> &bindings,
               const std::vector<MockEntry> &mocks) :
    FilesystemContext {root},
    MockEnvironment {root_path(), bindings, mocks},
    private_root_ {open_dir("/")}
  {
    try {
      if (::chroot(root_path().c_str()) != 0)
        system_error("enter scoped root");
      if (::chdir("/") != 0)
        system_error("enter scoped working directory");
      start();
    } catch (...) {
      cleanup("rollback scoped root");
      throw;
    }
  }

  void cleanup(const char * msg) noexcept {
    try {
      stop();
      // Unmount outside chroot, but before leaving the private mount namespace.
      assert_owner();
      // pthread join can precede the kernel's final fs_struct release.
      if (::unshare(CLONE_FS) != 0)
        system_error("isolate filesystem state before restoration");
      if (::fchdir(private_root_) != 0 || ::chroot(".") != 0)
        system_error("leave scoped root");
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s: %s\n", msg, error.what());
      std::terminate();
    }
  }

  void report_worker_error() noexcept {
    for (const auto &error_ptr : take_worker_errors()) {
      try {
        std::rethrow_exception(error_ptr);
      } catch (const std::exception &error) {
        ADD_FAILURE() << "mock callback/IPC failure: " << error.what();
      } catch (...) {
        ADD_FAILURE() << "mock callback/IPC failure: unknown exception";
      }
    }
  }

  unique_fd private_root_;
};

class ScopedRootFsBuilder {
 public:
  explicit ScopedRootFsBuilder(path_t root) : root_dir_ {std::move(root)} {
    if (root_dir_.empty() || root_dir_.native().find('\0') != std::string::npos)
      throw std::invalid_argument {"invalid scoped root path"};
  }

  ScopedRootFsBuilder &bind(const path_t &src, path_t dst = {}) {
    if (!src.is_absolute() || src.native().find('\0') != std::string::npos)
      throw std::invalid_argument {"bind source must be absolute"};
    if (dst.empty()) dst = src;
    dst = detail::MockEnvironment::validate(dst);

    for (const auto &binding : binds_)
      if (binding.second == dst)
        throw std::invalid_argument {"duplicate bind destination: " + dst.string()};
    binds_.emplace_back(src, std::move(dst));
    return *this;
  }

  template <template <typename> class MockType = ::testing::StrictMock>
  ScopedRootFsBuilder &mock(const path_t &path) {
    const auto target = detail::MockEnvironment::validate(path);
    for (const auto &entry : mocks_)
      if (entry.target == target)
        throw std::invalid_argument {"duplicate mock target: " + target.string()};
    mocks_.push_back({target, [](path_t command, path_t original)
        -> std::unique_ptr<MockExecutable> {
      return std::make_unique<MockType<MockExecutable>>(
          std::move(command), std::move(original));
    }});
    return *this;
  }

  [[nodiscard]] ScopedRootFs build() const {
    return ScopedRootFs {root_dir_, binds_, mocks_};
  }

 private:
  path_t root_dir_;
  std::vector<ScopedRootFs::Binding> binds_;
  std::vector<ScopedRootFs::MockEntry> mocks_;
};

} // namespace gh4ck3r::filesystem::scoped_rootfs
