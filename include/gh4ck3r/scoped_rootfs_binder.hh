#pragma once
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include "scoped_rootfs_filesystem.hh"

namespace gh4ck3r::filesystem::scoped_rootfs::detail {

class ReadonlyBinder {
 public:
  using Binding = std::pair<path_t, path_t>;

  ReadonlyBinder(FilesystemContext &filesystem, const std::vector<Binding> &bindings) :
    filesystem_ {filesystem}
  {
    try {
      apply(bindings);
    } catch (...) {
      cleanup("rollback scoped bindings");
      throw;
    }
  }

  ~ReadonlyBinder() noexcept {
    cleanup("unbind scoped bindings");
  }

  ReadonlyBinder(const ReadonlyBinder &) = delete;
  ReadonlyBinder &operator=(const ReadonlyBinder &) = delete;

  bool under_binding(const path_t &logical) const {
    return std::any_of(bindings_.begin(), bindings_.end(),
        [&](const Binding &binding) { return is_subpath(logical, binding.second); });
  }

  path_t path_in_root(const path_t &logical) const {
    return filesystem_.root_path() / validate_path(logical).relative_path();
  }

  path_t target(const path_t &logical, bool directory, bool require_existing = false) {
    filesystem_.require_preparing();
    namespace fs = std::filesystem;
    auto path = filesystem_.root_path();
    const auto relative = validate_path(logical).relative_path();
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

  void bind_readonly(const path_t &source, const path_t &destination) {
    filesystem_.require_preparing();
    const auto flags = readonly_flags(source);
    mounts_.push_back(destination);
    if (::mount(source.c_str(), destination.c_str(), nullptr, MS_BIND, nullptr) != 0) {
      mounts_.pop_back();
      system_error("bind " + source.string());
    }
    if (::mount(nullptr, destination.c_str(), nullptr, flags, nullptr) != 0)
      system_error("make bind read-only " + destination.string());
  }

  static bool is_subpath(const path_t &path, const path_t &base) {
    const auto rel = path.lexically_relative(base);
    return !rel.empty() && *rel.begin() != "..";
  }

  static path_t validate_path(const path_t &path) {
    const auto invalid_argument = [&path](const std::string &msg) {
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
    return result;
  }

 private:
  void apply(const std::vector<Binding> &bindings) {
    filesystem_.require_preparing();
    bindings_ = bindings;
    std::sort(bindings_.begin(), bindings_.end(),
              [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
    for (const auto &[source, logical] : bindings_) {
      const auto canonical = std::filesystem::canonical(source);
      bind_readonly(canonical, target(logical, std::filesystem::is_directory(canonical)));
    }
  }

  void unbind() {
    if (mounts_.empty()) return;
    filesystem_.require_mount_access();
    while (!mounts_.empty()) {
      const auto &target = mounts_.back();
      if (::umount2(target.c_str(), 0) != 0)
        system_error("unmount scoped bind " + target.string());
      mounts_.pop_back();
    }
  }

  void cleanup(const char *msg) noexcept {
    try {
      unbind();
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s: %s\n", msg, error.what());
      std::terminate();
    }
  }
  [[noreturn]]
  static void system_error(const std::string &operation, int error = errno) {
    throw std::system_error {error, std::system_category(), operation};
  }

  static unsigned long readonly_flags(const path_t &source) {
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

  FilesystemContext &filesystem_;
  std::vector<Binding> bindings_;
  std::vector<path_t> mounts_;
};

} // namespace gh4ck3r::filesystem::scoped_rootfs::detail
