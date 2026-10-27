#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <elf.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <sys/auxv.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/un.h>
#include <gmock/gmock.h>
#include <gh4ck3r/file.hh>

namespace gh4ck3r::filesystem {

namespace rootfs_detail {

namespace fs = std::filesystem;
inline constexpr const char *ipc_dir = "/.mock_ipc";
inline constexpr const char *socket_path = "/.mock_ipc/sock";
inline constexpr const char *runner_path = "/.mock_ipc/runner";
inline constexpr const char *originals_dir = "/.mock_orig";
inline constexpr int transport_error = 125;
inline constexpr std::uint32_t protocol_magic = 0x52465332;
inline constexpr std::uint32_t max_arguments = 1024;
inline constexpr std::uint32_t max_string = 64 * 1024;
inline constexpr std::size_t max_payload = 1024 * 1024;
inline constexpr int timeout_seconds = 10;

[[noreturn]] inline void system_error(const std::string &operation, int error = errno) {
  throw std::system_error {error, std::system_category(), operation};
}

inline unique_fd duplicate_fd(int fd) {
  const int result = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
  if (result < 0) system_error("duplicate namespace directory");
  return unique_fd {result};
}

inline unique_fd namespace_fd(int directory) {
  const int fd = ::openat(directory, "mnt", O_RDONLY | O_CLOEXEC);
  if (fd < 0) system_error("open mount namespace");
  return unique_fd {fd};
}

[[nodiscard]]
inline bool is_subpath(const path_t& path, const path_t& base) {
  const auto rel = path.lexically_relative(base);
  return !rel.empty() && *rel.begin() != "..";
}

inline path_t logical_path(const path_t &path) {
  auto result = path.lexically_normal();
  if (result.filename().empty()) result = result.parent_path();
  if (!result.is_absolute() || result == "/" ||
      path.native().find('\0') != std::string::npos)
    throw std::invalid_argument {"expected an absolute non-root path: " + path.string()};
  for (const auto &part : path)
    if (part == "..")
      throw std::invalid_argument {"parent traversal in path: " + path.string()};
  if (is_subpath(result, ipc_dir) || is_subpath(result, originals_dir))
    throw std::invalid_argument {"reserved scoped root path: " + path.string()};
  return result;
}

inline unsigned long readonly_flags(const path_t &source) {
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

inline bool send_all(int fd, const void *data, std::size_t size) noexcept {
  const auto *bytes = static_cast<const char *>(data);
  while (size) {
    const auto sent = ::send(fd, bytes, size, MSG_NOSIGNAL);
    if (sent < 0 && errno == EINTR) continue;
    if (sent <= 0) return false;
    bytes += sent;
    size -= static_cast<std::size_t>(sent);
  }
  return true;
}

inline bool read_all(int fd, void *data, std::size_t size) noexcept {
  auto *bytes = static_cast<char *>(data);
  while (size) {
    const auto count = ::read(fd, bytes, size);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    bytes += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

inline bool set_timeouts(int fd) noexcept {
  const timeval timeout {timeout_seconds, 0};
  return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
         ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

struct RequestHeader {
  std::uint32_t magic;
  std::uint32_t argc;
  std::uint32_t command_size;
};

struct Response {
  std::uint32_t magic = protocol_magic;
  int action = 0;
  int code = transport_error;
  char original[PATH_MAX] {};
};

[[noreturn]] inline void stub_error() noexcept {
  constexpr char message[] = "mock command transport failed\n";
  const char *data = message;
  std::size_t remaining = sizeof(message) - 1;
  while (remaining) {
    const auto count = ::write(STDERR_FILENO, data, remaining);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    data += count;
    remaining -= static_cast<std::size_t>(count);
  }
  ::_exit(transport_error);
}

inline bool mock_execution_path(char (&command)[PATH_MAX]) noexcept {
  struct stat status {};
  if (::stat(originals_dir, &status) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) return false;
    stub_error();
  }
  if (!S_ISDIR(status.st_mode)) stub_error();
  const auto *executed = reinterpret_cast<const char *>(::getauxval(AT_EXECFN));
  if (!executed || !*executed || !::realpath(executed, command)) stub_error();
  char original[PATH_MAX];
  const auto prefix = std::strlen(originals_dir);
  const auto size = std::strlen(command);
  if (prefix + size >= sizeof(original)) return false;
  std::memcpy(original, originals_dir, prefix);
  std::memcpy(original + prefix, command, size + 1);
  if (::lstat(original, &status) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) return false;
    stub_error();
  }
  if (!S_ISREG(status.st_mode)) stub_error();
  return true;
}

// Registration is identified by the execution path, not argv[0] or socket state.
// Only C operations are used before the executable's C++ initialization.
__attribute__((constructor(101)))
inline void mock_stub_entry(int argc, char **argv, char **envp) noexcept {
  char command[PATH_MAX];
  if (!mock_execution_path(command)) return;
  if (argc <= 0 || static_cast<unsigned int>(argc) > max_arguments) stub_error();

  const int sock = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock < 0 || !set_timeouts(sock)) stub_error();
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);
  if (::connect(sock, reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) != 0) stub_error();

  const auto command_size = static_cast<std::uint32_t>(std::strlen(command));
  RequestHeader header {protocol_magic, static_cast<std::uint32_t>(argc), command_size};
  iovec iov {&header, sizeof(header)};
  union Control {
    char bytes[CMSG_SPACE(sizeof(int) * 3)];
    cmsghdr alignment;
  } control {};
  msghdr message {};
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  message.msg_control = control.bytes;
  message.msg_controllen = sizeof(control.bytes);
  auto *cmsg = CMSG_FIRSTHDR(&message);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 3);
  const int descriptors[] {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
  std::memcpy(CMSG_DATA(cmsg), descriptors, sizeof(descriptors));
  ssize_t sent;
  do {
    sent = ::sendmsg(sock, &message, MSG_NOSIGNAL);
  } while (sent < 0 && errno == EINTR);
  if (sent <= 0) stub_error();
  if (!send_all(sock, reinterpret_cast<const char *>(&header) + sent,
                sizeof(header) - static_cast<std::size_t>(sent))) stub_error();
  if (!send_all(sock, command, command_size)) stub_error();

  std::size_t total = command_size;
  for (int i = 0; i < argc; ++i) {
    const auto size = ::strnlen(argv[i], max_string + 1);
    if (size > max_string || (total += size) > max_payload) stub_error();
    const auto length = static_cast<std::uint32_t>(size);
    if (!send_all(sock, &length, sizeof(length)) ||
        !send_all(sock, argv[i], length)) stub_error();
  }
  Response response;
  if (!read_all(sock, &response, sizeof(response))) stub_error();
  ::close(sock);
  if (response.magic != protocol_magic) stub_error();
  if (response.action == 1) {
    if (!std::memchr(response.original, '\0', sizeof(response.original))) stub_error();
    ::execve(response.original, argv, envp);
    stub_error();
  }
  if (response.action != 0 || response.code < 0 || response.code > 255) stub_error();
  ::_exit(response.code);
}

struct RuntimeElfInfo {
  path_t interpreter;
  std::string soname;
};

template <typename Header, typename ProgramHeader, typename Dynamic>
inline RuntimeElfInfo runtime_elf_info(std::ifstream &input) {
  RuntimeElfInfo result;
  Header header {};
  input.clear();
  input.seekg(0);
  input.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!input || header.e_phentsize != sizeof(ProgramHeader))
    throw std::runtime_error {"invalid executable program headers"};
  std::vector<ProgramHeader> programs;
  for (unsigned int i = 0; i < header.e_phnum; ++i) {
    ProgramHeader program {};
    input.seekg(header.e_phoff + i * header.e_phentsize);
    input.read(reinterpret_cast<char *>(&program), sizeof(program));
    if (!input) throw std::runtime_error {"read executable program header"};
    programs.push_back(program);
    if (program.p_type != PT_INTERP) continue;
    if (program.p_filesz < 2 || program.p_filesz > PATH_MAX)
      throw std::runtime_error {"invalid executable interpreter"};
    std::string path(program.p_filesz, '\0');
    input.seekg(program.p_offset);
    input.read(path.data(), path.size());
    if (!input || path.back() != '\0')
      throw std::runtime_error {"read executable interpreter"};
    path.pop_back();
    result.interpreter = path;
  }

  std::uint64_t strings = 0;
  std::uint64_t strings_size = 0;
  std::optional<std::uint64_t> soname;
  for (const auto &program : programs) {
    if (program.p_type != PT_DYNAMIC) continue;
    for (std::uint64_t offset = 0; offset + sizeof(Dynamic) <= program.p_filesz;
         offset += sizeof(Dynamic)) {
      Dynamic entry {};
      input.seekg(program.p_offset + offset);
      input.read(reinterpret_cast<char *>(&entry), sizeof(entry));
      if (!input) throw std::runtime_error {"read executable dynamic entry"};
      if (entry.d_tag == DT_NULL) break;
      if (entry.d_tag == DT_STRTAB) strings = entry.d_un.d_ptr;
      if (entry.d_tag == DT_STRSZ) strings_size = entry.d_un.d_val;
      if (entry.d_tag == DT_SONAME) soname = entry.d_un.d_val;
    }
  }
  if (!soname) return result;
  if (*soname >= strings_size || *soname > UINT64_MAX - strings)
    throw std::runtime_error {"invalid runtime library name offset"};
  const auto address = strings + *soname;
  for (const auto &program : programs) {
    if (program.p_type != PT_LOAD || address < program.p_vaddr ||
        address - program.p_vaddr >= program.p_filesz) continue;
    const auto offset = address - program.p_vaddr;
    const auto available = std::min<std::uint64_t>(
        strings_size - *soname, program.p_filesz - offset);
    input.seekg(program.p_offset + offset);
    for (std::uint64_t i = 0; i < available && i < PATH_MAX; ++i) {
      char ch;
      if (!input.get(ch)) throw std::runtime_error {"read runtime library name"};
      if (ch == '\0') {
        if (result.soname.empty() || result.soname == "." || result.soname == ".." ||
            result.soname.find('/') != std::string::npos)
          throw std::runtime_error {"invalid runtime library name"};
        return result;
      }
      result.soname += ch;
    }
    throw std::runtime_error {"unterminated runtime library name"};
  }
  throw std::runtime_error {"runtime library name is outside its file segments"};
}

inline RuntimeElfInfo runtime_elf_info(const path_t &path, bool require_elf = false) {
  std::ifstream input {path, std::ios::binary};
  if (!input) throw std::runtime_error {"open runtime file: " + path.string()};
  std::array<unsigned char, EI_NIDENT> ident {};
  input.read(reinterpret_cast<char *>(ident.data()), ident.size());
  if (input.gcount() < SELFMAG || std::memcmp(ident.data(), ELFMAG, SELFMAG) != 0) {
    if (require_elf) throw std::runtime_error {"invalid executable ELF header"};
    return {};
  }
  if (!input) throw std::runtime_error {"incomplete runtime ELF header"};
  if (ident[EI_CLASS] == ELFCLASS64)
    return runtime_elf_info<Elf64_Ehdr, Elf64_Phdr, Elf64_Dyn>(input);
  if (ident[EI_CLASS] == ELFCLASS32)
    return runtime_elf_info<Elf32_Ehdr, Elf32_Phdr, Elf32_Dyn>(input);
  throw std::runtime_error {"unsupported runtime ELF class"};
}

inline std::map<path_t, path_t> runtime_files(const path_t &executable) {
  std::set<path_t> mapped;
  std::ifstream maps {"/proc/self/maps"};
  if (!maps) throw std::runtime_error {"open executable memory map"};
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
    const auto [found, inserted] = result.emplace(destination, source);
    if (!inserted && fs::canonical(found->second) != fs::canonical(source))
      throw std::runtime_error {"conflicting runtime library: " + destination.string()};
  };
  for (const auto &path : mapped) {
    add(path, path);
    const auto info = runtime_elf_info(path);
    // /proc/self/maps contains resolved version files, not necessarily the
    // DT_SONAME filenames searched by the dynamic loader.
    if (!info.soname.empty()) add(path.parent_path() / info.soname, path);
  }
  const auto info = runtime_elf_info(executable, true);
  if (!info.interpreter.empty()) add(info.interpreter, fs::canonical(info.interpreter));
  return result;
}

} // namespace rootfs_detail

class ExecutableMock {
 public:
  explicit ExecutableMock(path_t target, path_t original) :
    target_path_ {std::move(target)}, orig_chroot_path_ {std::move(original)}
  {
    using ::testing::_;
    ON_CALL(*this, execute(_, _, _, _))
      .WillByDefault(::testing::Invoke(this, &ExecutableMock::execute_original));
  }
  virtual ~ExecutableMock() noexcept = default;
  MOCK_METHOD(int, execute, (const std::vector<std::string> &argv,
                            int stdin_fd, int stdout_fd, int stderr_fd));
  int execute_original(const std::vector<std::string> &, int, int, int) noexcept {
    exec_original_requested_ = true;
    return 0;
  }
  const path_t &path() const noexcept { return target_path_; }
  const path_t &original_path() const noexcept { return orig_chroot_path_; }

 private:
  friend class ScopedRootFilesystem;
  path_t target_path_;
  path_t orig_chroot_path_;
  bool exec_original_requested_ = false;
};

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
    remove_root();
    report_worker_error();
  }

  ExecutableMock &mock(const path_t &target) const {
    const auto path = rootfs_detail::logical_path(target);
    const auto found = mocks_.find(path);
    if (found == mocks_.end())
      throw std::out_of_range {"mock not found for path: " + path.string()};
    return *found->second;
  }

 private:
  friend class RootFilesystemFactory;
  using mock_builder_t = std::function<std::unique_ptr<ExecutableMock>(path_t, path_t)>;
  struct MockEntry {
    path_t target;
    mock_builder_t builder;
  };
  using Binding = std::pair<path_t, path_t>;

  ScopedRootFilesystem(const path_t &root, const std::vector<Binding> &bindings,
                       const std::vector<MockEntry> &mocks) :
    host_root_dir_ {std::filesystem::absolute(root).lexically_normal()},
    previous_ {active_},
    old_root_ {::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC)},
    old_cwd_ {::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC)},
    namespace_dir_ { previous_ ?
        ::fcntl(previous_->namespace_dir_, F_DUPFD_CLOEXEC, 3) :
        ::open("/proc/thread-self/ns", O_RDONLY | O_DIRECTORY | O_CLOEXEC)
    },
    old_mnt_ns_ {rootfs_detail::namespace_fd(namespace_dir_)}
  {
    namespace fs = std::filesystem;
    if (fs::exists(host_root_dir_))
      throw std::invalid_argument {
        "root_dir must not exist prior to creation: " + host_root_dir_.string()};
    const auto executable = previous_ ? path_t {rootfs_detail::runner_path}
                                     : fs::read_symlink("/proc/self/exe");
    runtime_files_ = previous_ ? previous_->runtime_files_
                              : rootfs_detail::runtime_files(executable);
    try {
      if (!fs::create_directories(host_root_dir_))
        throw std::invalid_argument {
          "root_dir must not exist prior to creation: " + host_root_dir_.string()};
      owned_ = true;
      fs::permissions(host_root_dir_, fs::perms::owner_all);
      if (::unshare(CLONE_NEWNS) != 0)
        rootfs_detail::system_error("create scoped mount namespace");
      namespace_active_ = true;
      private_root_.emplace(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
      // An outer scope already made the tree private. Its chroot need not be a
      // mountpoint, so MS_PRIVATE on "/" cannot be repeated in a nested scope.
      if (!previous_ && ::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0)
        rootfs_detail::system_error("make scoped mounts private");

      for (const auto &binding : bindings) {
        const auto source = fs::canonical(binding.first);
        bind_readonly(source, target(binding.second, fs::is_directory(source)));
      }
      for (const auto &[path, source] : runtime_files_) {
        const auto destination = host_root_dir_ / path.relative_path();
        if (fs::exists(destination)) continue;
        bind_readonly(fs::canonical(source), target(path, false));
      }
      bind_readonly(executable, target(rootfs_detail::runner_path, false));
      setup_mocks(mocks);
      if (::chroot(host_root_dir_.c_str()) != 0)
        rootfs_detail::system_error("enter scoped root");
      entered_ = true;
      if (::chdir("/") != 0) rootfs_detail::system_error("enter scoped working directory");
      active_ = this;
      if (!mocks_.empty()) start_ipc_server();
    } catch (...) {
      try {
        restore();
      } catch (const std::exception &error) {
        std::fprintf(stderr, "rollback scoped root: %s\n", error.what());
        std::terminate();
      }
      remove_root();
      throw;
    }
  }

  path_t target(const path_t &logical, bool directory, bool require_existing = false) {
    namespace fs = std::filesystem;
    auto path = host_root_dir_;
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
        if (fd < 0) rootfs_detail::system_error("create bind destination " + path.string());
        unique_fd file {fd};
      }
    }
    return path;
  }

  void bind_readonly(const path_t &source, const path_t &destination) {
    const auto flags = rootfs_detail::readonly_flags(source);
    mounts_.push_back(destination);
    if (::mount(source.c_str(), destination.c_str(), nullptr, MS_BIND, nullptr) != 0) {
      mounts_.pop_back();
      rootfs_detail::system_error("bind " + source.string());
    }
    if (::mount(nullptr, destination.c_str(), nullptr, flags, nullptr) != 0)
      rootfs_detail::system_error("make bind read-only " + destination.string());
  }

  void setup_mocks(const std::vector<MockEntry> &entries) {
    namespace fs = std::filesystem;
    for (const auto &entry : entries) {
      const auto destination = target(entry.target, false, true);
      if (!fs::is_regular_file(fs::symlink_status(destination)))
        throw std::invalid_argument {"mock requires a regular file: " + entry.target.string()};
      const auto original = path_t {rootfs_detail::originals_dir} / entry.target.relative_path();
      bind_readonly(destination, target(original, false));
      bind_readonly(host_root_dir_ / path_t {rootfs_detail::runner_path}.relative_path(),
                    destination);
      mocks_.emplace(entry.target, entry.builder(entry.target, original));
    }
  }

  void start_ipc_server() {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) rootfs_detail::system_error("create mock listener");
    listen_sock_.emplace(fd);
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, rootfs_detail::socket_path, sizeof(address.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
      rootfs_detail::system_error("bind mock listener");
    socket_owned_ = true;
    if (::chmod(rootfs_detail::socket_path, 0600) != 0 || ::listen(fd, 32) != 0)
      rootfs_detail::system_error("prepare mock listener");
    int pipe[2];
    if (::pipe2(pipe, O_CLOEXEC | O_NONBLOCK) != 0)
      rootfs_detail::system_error("create mock shutdown pipe");
    stop_pipe_r_.emplace(pipe[0]);
    stop_pipe_w_.emplace(pipe[1]);
    std::packaged_task<void()> task {[this] { ipc_loop(); }};
    worker_result_ = task.get_future();
    ipc_thread_ = std::thread {std::move(task)};
  }

  void stop_ipc_server() {
    if (ipc_thread_.joinable()) {
      const char wakeup = 1;
      ssize_t written;
      do {
        written = ::write(*stop_pipe_w_, &wakeup, sizeof(wakeup));
      } while (written < 0 && errno == EINTR);
      if (written != sizeof(wakeup))
        rootfs_detail::system_error("stop mock listener");
      ipc_thread_.join();
    }
    listen_sock_.reset();
    stop_pipe_r_.reset();
    stop_pipe_w_.reset();
    if (socket_owned_) {
      if (::unlink(rootfs_detail::socket_path) != 0 && errno != ENOENT)
        rootfs_detail::system_error("remove mock socket");
      socket_owned_ = false;
    }
  }

  void ipc_loop() {
    for (;;) {
      pollfd events[] {{*listen_sock_, POLLIN, 0}, {*stop_pipe_r_, POLLIN, 0}};
      const int ready = ::poll(events, 2, -1);
      if (ready < 0 && errno == EINTR) continue;
      if (ready < 0) rootfs_detail::system_error("poll mock listener");
      if (events[1].revents & POLLIN) return;
      if (!(events[0].revents & POLLIN))
        throw std::runtime_error {"unexpected mock listener event"};
      const int fd = ::accept4(*listen_sock_, nullptr, nullptr, SOCK_CLOEXEC);
      if (fd < 0 && errno == EINTR) continue;
      if (fd < 0) rootfs_detail::system_error("accept mock client");
      handle_client(unique_fd {fd});
    }
  }

  void handle_client(unique_fd client) {
    try {
      if (!rootfs_detail::set_timeouts(client))
        rootfs_detail::system_error("set mock IPC timeout");
      rootfs_detail::RequestHeader header {};
      iovec iov {&header, sizeof(header)};
      union Control {
        char bytes[CMSG_SPACE(sizeof(int) * 3)];
        cmsghdr alignment;
      } control {};
      msghdr message {};
      message.msg_iov = &iov;
      message.msg_iovlen = 1;
      message.msg_control = control.bytes;
      message.msg_controllen = sizeof(control.bytes);
      ssize_t count;
      do {
        count = ::recvmsg(client, &message, MSG_CMSG_CLOEXEC);
      } while (count < 0 && errno == EINTR);
      struct PassedFds {
        std::array<int, 3> descriptors {{-1, -1, -1}};
        ~PassedFds() {
          for (const auto fd : descriptors) if (fd >= 0) ::close(fd);
        }
      } passed;
      std::size_t descriptor_count = 0;
      if (count > 0) {
        for (auto *cmsg = CMSG_FIRSTHDR(&message); cmsg;
             cmsg = CMSG_NXTHDR(&message, cmsg)) {
          if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) continue;
          if (cmsg->cmsg_len < CMSG_LEN(0))
            throw std::runtime_error {"invalid mock descriptor header"};
          const auto size = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
          const auto *fds = reinterpret_cast<const int *>(CMSG_DATA(cmsg));
          for (std::size_t i = 0; i < size; ++i) {
            if (descriptor_count < passed.descriptors.size())
              passed.descriptors[descriptor_count++] = fds[i];
            else {
              ::close(fds[i]);
              message.msg_flags |= MSG_CTRUNC;
            }
          }
        }
      }
      if (count <= 0 || (message.msg_flags & MSG_CTRUNC) ||
          descriptor_count != passed.descriptors.size())
        throw std::runtime_error {"invalid mock IPC descriptors"};
      if (!rootfs_detail::read_all(client, reinterpret_cast<char *>(&header) + count,
              sizeof(header) - static_cast<std::size_t>(count)))
        throw std::runtime_error {"incomplete mock request header"};
      if (header.magic != rootfs_detail::protocol_magic ||
          header.command_size == 0 || header.command_size >= PATH_MAX || header.argc == 0 ||
          header.argc > rootfs_detail::max_arguments)
        throw std::runtime_error {"invalid mock request header"};
      std::string command(header.command_size, '\0');
      if (!rootfs_detail::read_all(client, command.data(), command.size()) ||
          command.front() != '/' || command.find('\0') != std::string::npos)
        throw std::runtime_error {"invalid mock command path"};
      const auto found = mocks_.find(command);
      if (found == mocks_.end())
        throw std::runtime_error {"unregistered mock command: " + command};
      std::vector<std::string> argv;
      std::size_t total = header.command_size;
      for (std::uint32_t i = 0; i < header.argc; ++i) {
        std::uint32_t length = 0;
        if (!rootfs_detail::read_all(client, &length, sizeof(length)) ||
            length > rootfs_detail::max_string ||
            (total += length) > rootfs_detail::max_payload)
          throw std::runtime_error {"invalid mock argument size"};
        std::string argument(length, '\0');
        if (!rootfs_detail::read_all(client, argument.data(), argument.size()) ||
            argument.find('\0') != std::string::npos)
          throw std::runtime_error {"invalid mock argument"};
        argv.push_back(std::move(argument));
      }
      auto &mock = *found->second;
      argv.front() = mock.path().string();
      mock.exec_original_requested_ = false;
      rootfs_detail::Response response;
      response.code = mock.execute(argv, passed.descriptors[0],
                                   passed.descriptors[1], passed.descriptors[2]);
      if (mock.exec_original_requested_) {
        const auto original = mock.original_path().string();
        if (original.size() >= sizeof(response.original))
          throw std::runtime_error {"mock executable path is too long"};
        response.action = 1;
        std::memcpy(response.original, original.c_str(), original.size() + 1);
      } else if (response.code < 0 || response.code > 255) {
        throw std::runtime_error {"invalid mock exit status"};
      }
      if (!rootfs_detail::send_all(client, &response, sizeof(response)))
        rootfs_detail::system_error("send mock response");
    } catch (...) {
      const rootfs_detail::Response error;
      if (!rootfs_detail::send_all(client, &error, sizeof(error)))
        std::fprintf(stderr, "failed to send mock error response: %s\n", std::strerror(errno));
      throw;
    }
  }

  void restore() {
    if (owner_ != std::this_thread::get_id())
      throw std::runtime_error {"scoped root destroyed on a different thread"};
    stop_ipc_server();
    if (namespace_active_) {
      // pthread join can precede the kernel's final fs_struct release.
      if (::unshare(CLONE_FS) != 0)
        rootfs_detail::system_error("isolate filesystem state before restoration");
      if (entered_ && (::fchdir(*private_root_) != 0 || ::chroot(".") != 0 ||
                       ::chdir("/") != 0))
        rootfs_detail::system_error("leave scoped root");
      while (!mounts_.empty()) {
        if (::umount2(mounts_.back().c_str(), 0) != 0)
          rootfs_detail::system_error("unmount scoped bind " + mounts_.back().string());
        mounts_.pop_back();
      }
      if (::setns(old_mnt_ns_, CLONE_NEWNS) != 0)
        rootfs_detail::system_error("restore mount namespace");
      namespace_active_ = false;
      if (::fchdir(old_root_) != 0 || ::chroot(".") != 0 || ::fchdir(old_cwd_) != 0)
        rootfs_detail::system_error("restore root and working directory");
    }
    if (entered_) {
      active_ = previous_;
      entered_ = false;
    }
  }

  void remove_root() noexcept {
    if (!owned_) return;
    std::error_code error;
    std::filesystem::remove_all(host_root_dir_, error);
    if (error) ADD_FAILURE() << "remove scoped root " << host_root_dir_ << ": " << error.message();
    owned_ = false;
  }

  void report_worker_error() noexcept {
    if (!worker_result_.valid()) return;
    try {
      worker_result_.get();
    } catch (const std::exception &error) {
      ADD_FAILURE() << "mock callback/IPC failure: " << error.what();
    } catch (...) {
      ADD_FAILURE() << "mock callback/IPC failure: unknown exception";
    }
  }

  inline static thread_local ScopedRootFilesystem *active_ = nullptr;
  path_t host_root_dir_;
  ScopedRootFilesystem *previous_;
  std::thread::id owner_ = std::this_thread::get_id();
  unique_fd old_root_;
  unique_fd old_cwd_;
  unique_fd namespace_dir_;
  unique_fd old_mnt_ns_;
  std::optional<unique_fd> private_root_;
  std::vector<path_t> mounts_;
  std::map<path_t, path_t> runtime_files_;
  std::map<path_t, std::unique_ptr<ExecutableMock>> mocks_;
  std::optional<unique_fd> listen_sock_;
  std::optional<unique_fd> stop_pipe_r_;
  std::optional<unique_fd> stop_pipe_w_;
  std::thread ipc_thread_;
  std::future<void> worker_result_;
  bool owned_ = false;
  bool namespace_active_ = false;
  bool entered_ = false;
  bool socket_owned_ = false;
};

class RootFilesystemFactory {
 public:
  explicit RootFilesystemFactory(path_t root) : root_dir_ {std::move(root)} {
    if (root_dir_.empty() || root_dir_.native().find('\0') != std::string::npos)
      throw std::invalid_argument {"invalid scoped root path"};
  }

  RootFilesystemFactory &bind(const path_t &source, path_t destination = {}) {
    if (!source.is_absolute() || source.native().find('\0') != std::string::npos)
      throw std::invalid_argument {"bind source must be absolute"};
    if (destination.empty()) destination = source;
    destination = rootfs_detail::logical_path(destination);
    for (const auto &binding : binds_)
      if (binding.second == destination)
        throw std::invalid_argument {"duplicate bind destination: " + destination.string()};
    binds_.emplace_back(source, std::move(destination));
    return *this;
  }

  template <template <typename> class MockType = ::testing::StrictMock>
  RootFilesystemFactory &mock(const path_t &path) {
    const auto target = rootfs_detail::logical_path(path);
    for (const auto &entry : mocks_)
      if (entry.target == target)
        throw std::invalid_argument {"duplicate mock target: " + target.string()};
    mocks_.push_back({target, [](path_t command, path_t original)
        -> std::unique_ptr<ExecutableMock> {
      return std::make_unique<MockType<ExecutableMock>>(
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

} // namespace gh4ck3r::filesystem
