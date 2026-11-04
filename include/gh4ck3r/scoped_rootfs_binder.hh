#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <gh4ck3r/file.hh>

namespace gh4ck3r::filesystem::scoped_rootfs::detail {

struct system_error : std::system_error {
  explicit system_error(const std::string &operation, int error = errno) :
    std::system_error {error, std::system_category(), operation}
  {}
};

inline unsigned long readonly_mountflags(const path_t &source) {
  struct statvfs status {};
  if (::statvfs(source.c_str(), &status) != 0)
    throw system_error {"inspect bind source " + source.string()};
  unsigned long flags = MS_BIND | MS_REMOUNT | MS_RDONLY;
  if (status.f_flag & ST_NOSUID) flags |= MS_NOSUID;
  if (status.f_flag & ST_NODEV) flags |= MS_NODEV;
  if (status.f_flag & ST_NOEXEC) flags |= MS_NOEXEC;
  if (status.f_flag & ST_NOATIME) flags |= MS_NOATIME;
  if (status.f_flag & ST_NODIRATIME) flags |= MS_NODIRATIME;
  if (status.f_flag & ST_RELATIME) flags |= MS_RELATIME;
  return flags;
}

class ReadonlyBinder {
 public:
  struct Binding {
    path_t src;
    path_t dst;
  };

  ReadonlyBinder(path_t root, std::vector<Binding> bindings) :
    root_path_ {std::move(root)}
  {
    std::sort(bindings.begin(), bindings.end(),
              [](const auto &lhs, const auto &rhs) { return lhs.dst < rhs.dst; });
    try {
      for (const auto &[src, dst] : bindings) bind(src, dst);
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

 protected:
  void bind(const path_t &host_src, const path_t &guest_dst) {
    const auto src = canonical(host_src);
    const auto flags = readonly_mountflags(src);
    const auto dst = ensure_mountpoint(guest_dst, is_directory(src));
    if (::mount(src.c_str(), dst.c_str(), nullptr, MS_BIND, nullptr) != 0)
      throw system_error {"bind " + src.string()};

    mounts_.push_back(dst);

    if (::mount(nullptr, dst.c_str(), nullptr, flags, nullptr) != 0)
      throw system_error {"make bind read-only " + dst.string()};
  }

  bool is_bound(const path_t &dst) const {
    const auto target = host_path(dst);
    return std::any_of(mounts_.begin(), mounts_.end(),
        [&](const path_t &mount) { return is_subpath(target, mount); });
  }

  path_t host_path(const path_t &dst) const {
    return root_path_ / guest_path(dst).relative_path();
  }

  static path_t guest_path(const path_t &dst) {
    if (dst.native().find('\0') != std::string::npos)
      throw std::invalid_argument {"malformed path: " + dst.native()};
    if (std::find(dst.begin(), dst.end(), "..") != dst.end())
      throw std::invalid_argument {"parent traversal in path: " + dst.native()};

    auto result = dst.lexically_normal();
    if (result.filename().empty()) result = result.parent_path();

    if (!result.is_absolute() || result == "/")
      throw std::invalid_argument {
        "expected an absolute non-root path: " + dst.native()};
    return result;
  }

 private:
  path_t ensure_mountpoint(const path_t &target, bool directory) {
    auto path = root_path_;
    for (const auto &part : guest_path(target).relative_path()) {
      path /= part;
      if (is_symlink(symlink_status(path)))
        throw std::invalid_argument {"symlink in bind destination: " + path.string()};
    }
    if (directory) {
      create_directories(path);
    } else if (!exists(path)) {
      create_directories(path.parent_path());
      std::ofstream {path}.exceptions(std::ios::failbit | std::ios::badbit);
    }
    return path;
  }

  void cleanup(const char *msg) noexcept {
    while (!mounts_.empty()) {
      const auto &target = mounts_.back();
      if (::umount2(target.c_str(), 0) != 0) {
        std::fprintf(stderr, "%s: unmount scoped bind %s: %s\n",
                     msg, target.c_str(), std::strerror(errno));
        std::terminate();
      }
      mounts_.pop_back();
    }
  }

  path_t root_path_;
  std::vector<path_t> mounts_;
};

} // namespace gh4ck3r::filesystem::scoped_rootfs::detail
