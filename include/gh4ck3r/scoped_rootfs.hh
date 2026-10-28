#pragma once
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <gmock/gmock.h>
#include <gh4ck3r/file.hh>
#include "scoped_rootfs_mock.hh"
#include "scoped_rootfs_elf.hh"

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
    try {
      restore();
    } catch (const std::exception &error) {
      std::fprintf(stderr, "restore scoped root: %s\n", error.what());
      std::terminate();
    }
    remove_private_root();
    report_worker_error();
  }

  mock::Executable &mock(const path_t &executable) const {
    return ipc_server_.mock(validate_private_path(executable));
  }

 private:
  friend class RootFilesystemFactory;
  using Binding = std::pair<path_t, path_t>;
  using MockEntry = mock::Entry;

  ScopedRootFilesystem(const path_t &root,
                       const std::vector<Binding> &bindings,
                       const std::vector<MockEntry> &mocks) :
    chroot_path_ {validate_chroot_path(root)},
    host_root_ {open_dir("/")},
    host_cwd_ {open_dir(".")},
    host_mnt_ns_ {open_mnt_ns()}
  {
    try {
      setup_newfs();
      setup_bindings(bindings);
      setup_mocks(mocks, bindings);
      setup_newroot();

      active_ = true;

      ipc_server_.start();
    } catch (...) {
      try {
        restore();
      } catch (const std::exception &error) {
        std::fprintf(stderr, "rollback scoped root: %s\n", error.what());
        std::terminate();
      }
      remove_private_root();
      throw;
    }
  }

  void setup_newfs() {
    if (::unshare(CLONE_NEWNS) != 0)
      system_error("create scoped mount namespace");
    private_root_.emplace(open_dir("/"));

    if (::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0)
      system_error("make scoped mounts private");

    namespace fs = std::filesystem;
    if (!fs::create_directories(chroot_path_)) throw std::invalid_argument {
        "root_dir must be created: " + chroot_path_.string()};
    fs::permissions(chroot_path_, fs::perms::owner_all);
  }

  void setup_bindings(const std::vector<Binding> &bindings)
  {
    namespace fs = std::filesystem;
    auto sorted_bindings = bindings;
    std::sort(sorted_bindings.begin(), sorted_bindings.end(),
              [](const auto &lhs, const auto &rhs) {
                return lhs.second < rhs.second;
              });
    for (const auto &binding : sorted_bindings) {
      const auto source = fs::canonical(binding.first);
      bind_readonly(source,
                    validate_target(binding.second, fs::is_directory(source)));
    }
  }

  path_t validate_target(const path_t &logical, bool directory, bool require_existing = false)
  {
    namespace fs = std::filesystem;
    auto path = chroot_path_;
    const auto relative = logical.relative_path();
    for (auto part = relative.begin(); part != relative.end(); ++part) {
      path /= *part;
      const auto status = fs::symlink_status(path);
      if (fs::is_symlink(status))
        throw std::invalid_argument {"symlink in bind destination: " + path.string()};

      const bool want_directory = std::next(part) != relative.end() || directory;
      if (fs::exists(status)) {
        if (want_directory != fs::is_directory(status))
          throw std::invalid_argument {"incompatible bind destination: " + path.string()};
      } else if (require_existing) {
        throw std::invalid_argument {"mock target does not exist: " + path.string()};
      } else if (want_directory) {
        fs::create_directory(path);
      } else {
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) system_error("create bind destination " + path.string());
        ::close(fd);
      }
    }
    return path;
  }

  inline static unsigned long readonly_flags(const path_t &source) {
    struct statvfs status {};
    if (::statvfs(source.c_str(), &status) != 0)
      system_error("inspect bind source " + source.string());
    unsigned long flags = MS_BIND | MS_REMOUNT | MS_RDONLY;
    if (status.f_flag & ST_NOSUID) flags |= MS_NOSUID;
    if (status.f_flag & ST_NODEV) flags |= MS_NODEV;
    if (status.f_flag & ST_NOEXEC) flags |= MS_NOEXEC;
    if (status.f_flag & ST_NOATIME) flags |= MS_NOATIME;
    if (status.f_flag & ST_NODIRATIME) flags |= MS_NODIRATIME;
    if (status.f_flag & ST_RELATIME) flags |= MS_RELATIME;
    return flags;
  }

  void bind_readonly(const path_t &source, const path_t &destination) {
    const auto flags = readonly_flags(source);
    mounts_.push_back(destination);
    if (::mount(source.c_str(), destination.c_str(), nullptr, MS_BIND, nullptr) != 0) {
      mounts_.pop_back();
      system_error("bind " + source.string());
    }
    if (::mount(nullptr, destination.c_str(), nullptr, flags, nullptr) != 0)
      system_error("make bind read-only " + destination.string());
  }

  void unbind_readonly() {
    while (!mounts_.empty()) {
      const auto target = std::move(mounts_.back());
      mounts_.pop_back();
      if (::umount2(target.c_str(), 0) != 0)
        system_error("unmount scoped bind " + target.string());
    }
  }

  void setup_mocks(const std::vector<MockEntry> &entries, const std::vector<Binding> &bindings) {
    if (entries.empty()) return;

    namespace fs = std::filesystem;
    const auto executable = fs::read_symlink("/proc/self/exe");
    bind_readonly(executable, validate_target(mock::stub_path, false));

    const auto is_under_binding = [&](const path_t &logical) {
      return std::any_of(bindings.begin(), bindings.end(),
          [&](const Binding &binding) {
            return logical == binding.second ||
                    is_subpath(logical, binding.second);
          });
    };
    for (const auto &[path, source] : runtime_files(executable)) {
      auto logical = path;
      auto destination = chroot_path_ / logical.relative_path();
      if (fs::exists(fs::symlink_status(destination))) continue;
      if (is_under_binding(logical)) {
        logical = fallback_runtime_path(path);
        destination = chroot_path_ / logical.relative_path();
        if (fs::exists(fs::symlink_status(destination)) || is_under_binding(logical))
          continue;
      }
      bind_readonly(source, validate_target(logical, false));
    }

    for (const auto &entry : entries) {
      const auto destination = validate_target(entry.target, false, true);
      if (!fs::is_regular_file(fs::symlink_status(destination)))
        throw std::invalid_argument {"mock requires a regular file: " + entry.target.string()};
      const auto original = path_t {mock::originals_dir} / entry.target.relative_path();
      bind_readonly(destination, validate_target(original, false));
      bind_readonly(chroot_path_ / path_t {mock::stub_path}.relative_path(),
                    destination);
      ipc_server_.emplace(entry.target, entry.builder(entry.target, original));
    }
  }

  static inline std::map<path_t, path_t> runtime_files(const path_t &executable) {
    std::set<path_t> mapped;
    std::ifstream maps {"/proc/self/maps"};
    if (!maps) throw std::runtime_error {"open executable memory map"};

    namespace fs = std::filesystem;
    for (std::string line; std::getline(maps, line);) {
      const auto slash = line.find('/');
      if (slash == std::string::npos) continue;
      const std::string path = line.substr(slash);
      const std::string deleted = " (deleted)";
      if (path.size() >= deleted.size() &&
          path.compare(path.size() - deleted.size(), deleted.size(), deleted) == 0)
        throw std::runtime_error {"mapped runtime file was deleted: " + path};
      if (path != executable && fs::is_regular_file(path)) mapped.insert(path);
    }

    if (maps.bad()) throw std::runtime_error {"read executable memory map"};
    std::map<path_t, path_t> result;
    const auto add = [&](const path_t &destination, const path_t &source) {
      const auto canonical_src = fs::canonical(source);
      const auto [found, inserted] = result.emplace(destination, canonical_src);
      if (!inserted && found->second != canonical_src)
        throw std::runtime_error {"conflicting runtime library: " + destination.string()};
    };
    for (const auto &path : mapped) {
      add(path, path);
      const auto info = elf::runtime_elf_info(path);
      // /proc/self/maps contains resolved version files, not necessarily the
      // DT_SONAME filenames searched by the dynamic loader.
      if (!info.soname.empty()) add(path.parent_path() / info.soname, path);
    }
    const auto info = elf::runtime_elf_info(executable, true);
    if (!info.interpreter.empty()) add(info.interpreter, info.interpreter);
    return result;
  }

  void setup_newroot()
  {
    if (::chroot(chroot_path_.c_str()) != 0)
      system_error("enter scoped root");
    if (::chdir("/") != 0) system_error("enter scoped working directory");
  }

  void restore() {
    if (owner_ != std::this_thread::get_id())
      throw std::runtime_error {"scoped root destroyed on a different thread"};
    ipc_server_.stop();
    if (private_root_) {
      // pthread join can precede the kernel's final fs_struct release.
      if (::unshare(CLONE_FS) != 0)
        system_error("isolate filesystem state before restoration");
      if (::fchdir(*private_root_) != 0 || ::chroot(".") != 0)
        system_error("leave scoped root");

      unbind_readonly();

      if (::setns(host_mnt_ns_, CLONE_NEWNS) != 0)
        system_error("restore mount namespace");
      if (::fchdir(host_root_) != 0 || ::chroot(".") != 0 || ::fchdir(host_cwd_) != 0)
        system_error("restore root and working directory");

      private_root_.reset();
    }
    active_ = false;
  }

  void remove_private_root() noexcept {
    std::error_code error;
    std::filesystem::remove_all(chroot_path_, error);
    if (error) ADD_FAILURE() << "remove scoped root " << chroot_path_ << ": " << error.message();
  }

  void report_worker_error() noexcept {
    for (const auto &error_ptr : ipc_server_.take_worker_errors()) {
      try {
        std::rethrow_exception(error_ptr);
      } catch (const std::exception &error) {
        ADD_FAILURE() << "mock callback/IPC failure: " << error.what();
      } catch (...) {
        ADD_FAILURE() << "mock callback/IPC failure: unknown exception";
      }
    }
  }

  [[nodiscard]]
  static inline bool is_subpath(const path_t& path, const path_t& base) {
    const auto rel = path.lexically_relative(base);
    return !rel.empty() && *rel.begin() != "..";
  }

  static inline path_t validate_chroot_path(const path_t &root) {
    if (active_)
      throw std::logic_error {"scoped root is already active on this thread"};
    namespace fs = std::filesystem;
    auto dir = fs::absolute(root).lexically_normal();
    if (fs::exists(fs::symlink_status(dir)))
      throw std::invalid_argument {"root_dir must not exist prior to creation: " + dir.string()};
    return dir;
  }

  static inline path_t validate_private_path(const path_t &path) {
    const auto invalid_argument = [&path] (std::string msg) {
      throw std::invalid_argument {msg + ": " + path.native()};
    };

    if (path.native().find('\0') != std::string::npos)
      invalid_argument("malformed path");

    if (std::find(path.begin(), path.end(), "..") != path.end())
      invalid_argument("parent traversal in path");

    auto result = path.lexically_normal();
    if (result.filename().empty()) result = result.parent_path();
    if (!result.is_absolute() || result == "/")
      invalid_argument("expected an absolute non-root path");

    for (const auto &p : {mock::ipc_dir, mock::originals_dir}) {
      if (is_subpath(result, p)) invalid_argument("reserved scoped root path");
    }

    return result;
  }

  [[noreturn]]
  static inline void system_error(const std::string &operation, int error = errno) {
    throw std::system_error {error, std::system_category(), operation};
  }

  static inline path_t fallback_runtime_path(const path_t &path) {
    if (is_subpath(path, "/usr/lib"))
      return path_t {"/lib"} / path.lexically_relative("/usr/lib");
    if (is_subpath(path, "/lib"))
      return path_t {"/usr/lib"} / path.lexically_relative("/lib");
    if (is_subpath(path, "/usr/lib64"))
      return path_t {"/lib64"} / path.lexically_relative("/usr/lib64");
    if (is_subpath(path, "/lib64"))
      return path_t {"/usr/lib64"} / path.lexically_relative("/lib64");
    return path_t {"/lib"} / path.filename();
  }

  [[nodiscard]]
  static inline unique_fd promote_fd(int fd) {
    if (const int high_fd = ::fcntl(fd, F_DUPFD_CLOEXEC, 1000); high_fd != -1) {
      ::close(fd);
      return {high_fd};
    }
    return {fd};
  }

  [[nodiscard]]
  static inline unique_fd open_dir(const path_t &dir, int flags = O_PATH | O_DIRECTORY | O_CLOEXEC) {
    const auto fd = ::open(dir.c_str(), flags);
    if (fd == -1) system_error("open directory " + dir.string());
    return promote_fd(fd);
  }

  [[nodiscard]]
  static inline unique_fd open_mnt_ns() {
    const auto fd = ::open("/proc/thread-self/ns/mnt", O_RDONLY | O_CLOEXEC);
    if (fd == -1) system_error("open mount namespace");
    return promote_fd(fd);
  }

  inline static thread_local bool active_ = false;
  path_t chroot_path_;
  std::thread::id owner_ = std::this_thread::get_id();
  unique_fd host_root_;
  unique_fd host_cwd_;
  unique_fd host_mnt_ns_;
  std::optional<unique_fd> private_root_;
  std::vector<path_t> mounts_;
  mock::IpcServer ipc_server_;
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
    dst = ScopedRootFilesystem::validate_private_path(dst);

    for (const auto &binding : binds_)
      if (binding.second == dst)
        throw std::invalid_argument {"duplicate bind destination: " + dst.string()};
    binds_.emplace_back(src, std::move(dst));
    return *this;
  }

  template <template <typename> class MockType = ::testing::StrictMock>
  RootFilesystemFactory &mock(const path_t &path) {
    const auto target = ScopedRootFilesystem::validate_private_path(path);
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
