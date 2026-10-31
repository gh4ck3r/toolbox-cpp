#pragma once
#include <exception>
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

class MockEnvironment {
 public:
  MockEnvironment(ReadonlyBinder &binder, const std::vector<mock::Entry> &entries) :
    binder_ {binder}
  {
    prepare(entries);
  }

  void start() { server_.start(); }
  void stop() { server_.stop(); }

  mock::Executable &mock(const path_t &executable) const {
    return server_.mock(validate_path(executable));
  }

  std::vector<std::exception_ptr> take_worker_errors() {
    return server_.take_worker_errors();
  }

  static path_t validate_path(const path_t &path) {
    const auto result = ReadonlyBinder::validate_path(path);
    for (const auto &p : {mock::ipc_dir, mock::originals_dir}) {
      if (ReadonlyBinder::is_subpath(result, p))
        throw std::invalid_argument {"reserved scoped root path: " + path.native()};
    }
    return result;
  }

 private:
  void prepare(const std::vector<mock::Entry> &entries) {
    if (entries.empty()) return;
    namespace fs = std::filesystem;
    const auto executable = fs::read_symlink("/proc/self/exe");
    binder_.bind_readonly(executable, binder_.target(mock::stub_path, false));
    for (const auto &[path, source] : runtime_files(executable)) {
      auto logical = path;
      auto destination = binder_.path_in_root(logical);
      if (fs::exists(fs::symlink_status(destination))) continue;
      if (binder_.under_binding(logical)) {
        logical = fallback_runtime_path(path);
        destination = binder_.path_in_root(logical);
        if (fs::exists(fs::symlink_status(destination)) || binder_.under_binding(logical))
          continue;
      }
      binder_.bind_readonly(source, binder_.target(logical, false));
    }

    for (const auto &entry : entries) {
      const auto destination = binder_.target(entry.target, false, true);
      if (!fs::is_regular_file(fs::symlink_status(destination)))
        throw std::invalid_argument {"mock requires a regular file: " + entry.target.string()};
      const auto original = path_t {mock::originals_dir} / entry.target.relative_path();
      binder_.bind_readonly(destination, binder_.target(original, false));
      binder_.bind_readonly(binder_.path_in_root(mock::stub_path), destination);
      server_.emplace(entry.target, entry.builder(entry.target, original));
    }
  }
  static path_t fallback_runtime_path(const path_t &path) {
    if (ReadonlyBinder::is_subpath(path, "/usr/lib"))
      return path_t {"/lib"} / path.lexically_relative("/usr/lib");
    if (ReadonlyBinder::is_subpath(path, "/lib"))
      return path_t {"/usr/lib"} / path.lexically_relative("/lib");
    if (ReadonlyBinder::is_subpath(path, "/usr/lib64"))
      return path_t {"/lib64"} / path.lexically_relative("/usr/lib64");
    if (ReadonlyBinder::is_subpath(path, "/lib64"))
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

  ReadonlyBinder &binder_;
  mock::IpcServer server_;
};

} // namespace gh4ck3r::filesystem::scoped_rootfs::detail
