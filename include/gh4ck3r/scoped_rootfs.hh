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
#include "scoped_rootfs_binder.hh"
#include "scoped_rootfs_mock_environment.hh"

namespace gh4ck3r::filesystem::scoped_rootfs {

class RootFilesystemFactory;

// Thread-bound, non-copyable guard. Binds are non-recursive and read-only.
// Commands must have completed before destruction; additional mounts must be
// released by their owner before this guard restores the previous filesystem.
class ScopedRootFilesystem {
 public:
  ScopedRootFilesystem(const ScopedRootFilesystem &) = delete;
  ScopedRootFilesystem &operator=(const ScopedRootFilesystem &) = delete;
  ScopedRootFilesystem(ScopedRootFilesystem &&) = delete;
  ScopedRootFilesystem &operator=(ScopedRootFilesystem &&) = delete;

  ~ScopedRootFilesystem() noexcept {
    cleanup("restore scoped root");
    report_worker_error();
  }

  mock::Executable &mock(const path_t &executable) const {
    return mocks_.mock(executable);
  }

 private:
  friend class RootFilesystemFactory;
  using Binding = detail::ReadonlyBinder::Binding;
  using MockEntry = mock::Entry;

  ScopedRootFilesystem(const path_t &root,
                       const std::vector<Binding> &bindings,
                       const std::vector<MockEntry> &mocks) :
    filesystem_ {root},
    binder_ {filesystem_},
    mocks_ {binder_}
  {
    try {
      filesystem_.prepare();
      binder_.apply(bindings);
      mocks_.prepare(mocks);
      filesystem_.enter();
      mocks_.start();
    } catch (...) {
      cleanup("rollback scoped root");
      throw;
    }
  }

  void cleanup(const char * msg) noexcept {
    try {
      restore();
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s: %s\n", msg, error.what());
      std::terminate();
    }
    remove_private_root();
  }

  void restore() {
    filesystem_.assert_owner();
    mocks_.stop();
    // Unmount outside chroot, but before leaving the private mount namespace.
    filesystem_.leave();
    binder_.unbind();
    filesystem_.restore();
  }

  void remove_private_root() noexcept {
    if (const auto error = filesystem_.remove_root())
      ADD_FAILURE() << "remove scoped root " << filesystem_.root_path() << ": " << error.message();
  }

  void report_worker_error() noexcept {
    for (const auto &error_ptr : mocks_.take_worker_errors()) {
      try {
        std::rethrow_exception(error_ptr);
      } catch (const std::exception &error) {
        ADD_FAILURE() << "mock callback/IPC failure: " << error.what();
      } catch (...) {
        ADD_FAILURE() << "mock callback/IPC failure: unknown exception";
      }
    }
  }

  detail::FilesystemContext filesystem_;
  detail::ReadonlyBinder binder_;
  detail::MockEnvironment mocks_;
};

class RootFilesystemFactory {
 public:
  explicit RootFilesystemFactory(path_t root) : root_dir_ {std::move(root)} {
    if (root_dir_.empty() || root_dir_.native().find('\0') != std::string::npos)
      throw std::invalid_argument {"invalid scoped root path"};
  }

  RootFilesystemFactory &bind(const path_t &src, path_t dst = {}) {
    if (!src.is_absolute() || src.native().find('\0') != std::string::npos)
      throw std::invalid_argument {"bind source must be absolute"};
    if (dst.empty()) dst = src;
    dst = detail::MockEnvironment::validate_path(dst);

    for (const auto &binding : binds_)
      if (binding.second == dst)
        throw std::invalid_argument {"duplicate bind destination: " + dst.string()};
    binds_.emplace_back(src, std::move(dst));
    return *this;
  }

  template <template <typename> class MockType = ::testing::StrictMock>
  RootFilesystemFactory &mock(const path_t &path) {
    const auto target = detail::MockEnvironment::validate_path(path);
    for (const auto &entry : mocks_)
      if (entry.target == target)
        throw std::invalid_argument {"duplicate mock target: " + target.string()};
    mocks_.push_back({target, [](path_t command, path_t original)
        -> std::unique_ptr<mock::Executable> {
      return std::make_unique<MockType<mock::Executable>>(
          std::move(command), std::move(original));
    }});
    return *this;
  }

  [[nodiscard]] ScopedRootFilesystem create() const {
    return ScopedRootFilesystem {root_dir_, binds_, mocks_};
  }

 private:
  path_t root_dir_;
  std::vector<ScopedRootFilesystem::Binding> binds_;
  std::vector<ScopedRootFilesystem::MockEntry> mocks_;
};

} // namespace gh4ck3r::filesystem::scoped_rootfs
