#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "scoped_rootfs_binder.hh"
#include "scoped_rootfs_elf.hh"
#include "scoped_rootfs_mock.hh"

namespace gh4ck3r::filesystem::scoped_rootfs::detail {

class MockEnvironment : ReadonlyBinder, protected MockExecutableServer {
 public:
  using ReadonlyBinder::Binding;
  using MockExecutableServer::Entry;

  MockEnvironment(path_t root, const std::vector<Binding> &bindings,
                  const std::vector<Entry> &entries) :
    ReadonlyBinder {std::move(root), bindings}
  {
    if (entries.empty()) return;
    namespace fs = std::filesystem;
    const auto executable = fs::read_symlink("/proc/self/exe");
    bind(executable, ensure_mountpoint(stub_path, false));
    for (const auto &[path, source] : runtime_files(executable)) {
      auto guest_path = path;
      auto destination = host_path(guest_path);
      if (fs::exists(fs::symlink_status(destination))) continue;
      if (is_bound(guest_path)) {
        guest_path = fallback_runtime_path(path);
        destination = host_path(guest_path);
        if (fs::exists(fs::symlink_status(destination)) || is_bound(guest_path))
          continue;
      }
      bind(source, ensure_mountpoint(guest_path, false));
    }

    for (const auto &entry : entries) {
      const auto destination = host_path(entry.target);
      const auto status = fs::symlink_status(destination);
      if (!fs::exists(status))
        throw std::invalid_argument {"mock target does not exist: " + destination.string()};
      if (!fs::is_regular_file(status))
        throw std::invalid_argument {"mock requires a regular file: " + entry.target.string()};
      const auto original = path_t {originals_dir} / entry.target.relative_path();
      bind(destination, ensure_mountpoint(original, false));
      bind(host_path(stub_path), destination);
      emplace(entry.target, entry.builder(entry.target, original));
    }
  }
  ~MockEnvironment() = default;

  MockExecutable &mock(const path_t &executable) const {
    return MockExecutableServer::mock(validate(executable));
  }

  static path_t validate(const path_t &path) {
    const auto result = ReadonlyBinder::validate(path);
    for (const auto &p : {ipc_dir, originals_dir}) {
      if (is_subpath(result, p))
        throw std::invalid_argument {"reserved scoped root path: " + path.native()};
    }
    return result;
  }

 private:
  static path_t fallback_runtime_path(const path_t &path) {
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

  static std::map<path_t, path_t> runtime_files(const path_t &executable) {
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
      // The loader searches SONAME aliases, not just resolved /proc/self/maps paths.
      if (!info.soname.empty()) add(path.parent_path() / info.soname, path);
    }
    const auto info = elf::runtime_elf_info(executable, true);
    if (!info.interpreter.empty()) add(info.interpreter, info.interpreter);
    return result;
  }
};

} // namespace gh4ck3r::filesystem::scoped_rootfs::detail
