#pragma once
#include <gh4ck3r/file.hh>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <gmock/gmock.h>

namespace gh4ck3r::filesystem {

namespace detail {

inline constexpr const char *kMockIpcSockPath = "/.mock_ipc/sock";

struct StubResponse {
  int action; // 0 = _exit(code), 1 = execve(orig_path, argv, envp)
  int code;
  char orig_path[512];
};

inline bool write_all(int fd, const void *buf, size_t len) noexcept {
  const auto *p = static_cast<const char *>(buf);
  while (len > 0) {
    ssize_t w = ::write(fd, p, len);
    if (w <= 0) {
      if (w < 0 && errno == EINTR) continue;
      return false;
    }
    p += static_cast<size_t>(w);
    len -= static_cast<size_t>(w);
  }
  return true;
}

inline bool read_all(int fd, void *buf, size_t len) noexcept {
  auto *p = static_cast<char *>(buf);
  while (len > 0) {
    ssize_t r = ::read(fd, p, len);
    if (r <= 0) {
      if (r < 0 && errno == EINTR) continue;
      return false;
    }
    p += static_cast<size_t>(r);
    len -= static_cast<size_t>(r);
  }
  return true;
}

// Platform-independent stub entry point registered in .init_array.
// When the test binary itself (/proc/self/exe) is bind-mounted over a mocked executable
// (e.g. /bin/ls) and executed inside the scoped root filesystem, this function intercepts
// execution before main(), forwards {stdin, stdout, stderr, argv} to the parent's GMock
// server over "/.mock_ipc/sock", and either exits or execve()s the preserved original binary.
__attribute__((constructor(101)))
inline void mock_stub_entry(int argc, char **argv, char **envp) noexcept {
  if (::access(kMockIpcSockPath, F_OK) != 0) {
    return;
  }

  int sock = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock < 0) {
    ::_exit(127);
  }

  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, kMockIpcSockPath, sizeof(addr.sun_path) - 1);

  if (::connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    // Not a mocked child (or server not listening); proceed to normal execution.
    ::close(sock);
    return;
  }

  // 1. Send SCM_RIGHTS (stdin=0, stdout=1, stderr=2) + argc
  int argc_i = argc;
  iovec iov {&argc_i, sizeof(argc_i)};
  union {
    char buf[CMSG_SPACE(sizeof(int) * 3)];
    cmsghdr align;
  } u {};

  msghdr msg {};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = u.buf;
  msg.msg_controllen = sizeof(u.buf);

  cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 3);
  int *fds = reinterpret_cast<int *>(CMSG_DATA(cmsg));
  fds[0] = STDIN_FILENO;
  fds[1] = STDOUT_FILENO;
  fds[2] = STDERR_FILENO;

  while (::sendmsg(sock, &msg, 0) < 0) {
    if (errno == EINTR) continue;
    ::close(sock);
    ::_exit(127);
  }

  // 2. Send each argv[i] including its null terminator
  for (int i = 0; i < argc; ++i) {
    const char *s = argv[i] ? argv[i] : "";
    const size_t len = std::strlen(s) + 1;
    if (!write_all(sock, s, len)) {
      ::close(sock);
      ::_exit(127);
    }
  }

  // 3. Receive response from parent GMock server
  StubResponse resp {};
  const bool ok = read_all(sock, &resp, sizeof(resp));
  ::close(sock);
  if (!ok) {
    ::_exit(127);
  }

  // 4. Execute original binary if requested, otherwise exit with returned code
  if (resp.action == 1) {
    ::execve(resp.orig_path, argv, envp);
    ::_exit(126);
  }

  ::_exit(resp.code);
}

inline path_t to_relative(const path_t &p) {
  auto rel = p.lexically_normal().relative_path();
  if (rel == ".") return {};
  return rel;
}

inline path_t to_normalized_abs(const path_t &p) {
  if (!p.is_absolute()) {
    throw std::invalid_argument {"path must be absolute: " + p.string()};
  }
  return p.lexically_normal();
}

inline unique_fd open_high_fd(const path_t &p, int flags) {
  int fd = ::open(p.c_str(), flags | O_CLOEXEC);
  if (fd == -1) {
    throw std::system_error {
      errno, std::system_category(), "failed to open: " + p.string()
    };
  }
  if (int high_fd = ::fcntl(fd, F_DUPFD_CLOEXEC, 1000); high_fd != -1) {
    ::close(fd);
    fd = high_fd;
  }
  return unique_fd {fd};
}

// Parse /proc/self/maps to discover all shared libraries (and dynamic linker)
// mapped into the current test binary so that /proc/self/exe can run inside chroot
// even if the user did not explicitly bind all library directories.
inline std::set<path_t> collect_self_mapped_files() {
  std::set<path_t> files;
  const path_t self_exe = read_symlink("/proc/self/exe");

  std::ifstream maps {"/proc/self/maps"};
  std::string line;
  while (std::getline(maps, line)) {
    const auto slash_pos = line.find('/');
    if (slash_pos == std::string::npos) continue;

    std::string mapped_path = line.substr(slash_pos);
    if (mapped_path.ends_with(" (deleted)")) continue;

    path_t p {mapped_path};
    std::error_code ec;
    if (p == self_exe) continue;
    if (is_regular_file(p, ec) && !ec) {
      files.insert(p.lexically_normal());
    }
  }
  return files;
}

inline bool is_subpath_of_any(const path_t &target, const std::vector<path_t> &dirs) {
  for (const auto &d : dirs) {
    auto rel = target.lexically_relative(d);
    if (!rel.empty() && rel.native()[0] != '.') {
      return true;
    }
  }
  return false;
}

} // namespace detail

class ExecutableMock {
 public:
  explicit ExecutableMock(path_t target_path, path_t orig_chroot_path) :
    target_path_ {std::move(target_path)},
    orig_chroot_path_ {std::move(orig_chroot_path)}
  {
    ON_CALL(*this, execute(::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillByDefault([this](const std::vector<std::string> &argv,
                            int stdin_fd, int stdout_fd, int stderr_fd) {
        return this->execute_original(argv, stdin_fd, stdout_fd, stderr_fd);
      });
  }

  virtual ~ExecutableMock() noexcept = default;

  MOCK_METHOD(int, execute,
              (const std::vector<std::string> &argv,
               int stdin_fd,
               int stdout_fd,
               int stderr_fd));

  int execute_original(const std::vector<std::string> & /*argv*/,
                       int /*stdin_fd*/,
                       int /*stdout_fd*/,
                       int /*stderr_fd*/) noexcept {
    exec_original_requested_ = true;
    return 0;
  }

  const path_t &path() const noexcept { return target_path_; }
  const path_t &original_path() const noexcept { return orig_chroot_path_; }

 private:
  friend class ScopedRootFilesystem;

  void reset_invocation_state() noexcept {
    exec_original_requested_ = false;
  }

  bool is_exec_original_requested() const noexcept {
    return exec_original_requested_;
  }

  path_t target_path_;
  path_t orig_chroot_path_;
  bool exec_original_requested_ {false};
};

class RootFilesystemFactory;

class ScopedRootFilesystem {
 public:
  ScopedRootFilesystem(const ScopedRootFilesystem &) = delete;
  ScopedRootFilesystem &operator=(const ScopedRootFilesystem &) = delete;
  ScopedRootFilesystem(ScopedRootFilesystem &&) = delete;
  ScopedRootFilesystem &operator=(ScopedRootFilesystem &&) = delete;

  ~ScopedRootFilesystem() noexcept {
    stop_ipc_server();

    if (::fchdir(old_root_) == -1 || ::chroot(".") == -1) {
      std::cerr << "failed to restore root directory: "
                << std::strerror(errno) << '\n';
      std::terminate();
    }

    if (::setns(old_mnt_ns_, CLONE_NEWNS) == -1) {
      std::cerr << "failed to restore mount namespace: "
                << std::strerror(errno) << '\n';
      std::terminate();
    }

    if (::fchdir(old_cwd_) == -1) {
      std::cerr << "failed to restore current working directory: "
                << std::strerror(errno) << '\n';
      std::terminate();
    }

    std::error_code ec;
    remove_all(host_root_dir_, ec);
    if (ec) {
      std::cerr << "failed to remove scoped root directory " << host_root_dir_
                << ": " << ec.message() << '\n';
    }
  }

  ExecutableMock &mock(const path_t &target) const {
    const auto norm = detail::to_normalized_abs(target);
    auto it = mocks_.find(norm);
    if (it == mocks_.end()) {
      throw std::out_of_range {"mock not found for path: " + norm.string()};
    }
    return *(it->second);
  }

 private:
  friend class RootFilesystemFactory;

  using mock_builder_t = std::function<std::unique_ptr<ExecutableMock>(path_t, path_t)>;
  struct MockEntry {
    path_t target;
    mock_builder_t builder;
  };

  ScopedRootFilesystem(const path_t &root_dir,
                       const std::vector<path_t> &binds,
                       const std::vector<MockEntry> &mocks) :
    host_root_dir_ {absolute(root_dir).lexically_normal()},
    old_root_ {detail::open_high_fd("/", O_PATH | O_DIRECTORY)},
    old_cwd_ {detail::open_high_fd(".", O_PATH | O_DIRECTORY)},
    old_mnt_ns_ {detail::open_high_fd("/proc/self/ns/mnt", O_RDONLY)}
  {
    if (is_directory("/proc/self/task") && dir_siz("/proc/self/task") > 1) {
      throw std::runtime_error {
        "ScopedRootFilesystem can't be created when multiple threads are active"
      };
    }

    if (exists(host_root_dir_)) {
      throw std::invalid_argument {
        "root_dir must not exist prior to creation: " + host_root_dir_.string()
      };
    }

    std::vector<path_t> normalized_binds;
    normalized_binds.reserve(binds.size());
    for (const auto &b : binds) {
      const auto norm = detail::to_normalized_abs(b);
      if (!exists(norm)) {
        throw std::invalid_argument {
          "bind source does not exist: " + norm.string()
        };
      }
      normalized_binds.push_back(norm);
    }

    create_directories(host_root_dir_);

    try {
      if (::unshare(CLONE_NEWNS) == -1) {
        throw std::system_error {
          errno, std::system_category(), "failed to unshare mount namespace"
        };
      }

      if (::mount("none", "/", nullptr, MS_REC | MS_PRIVATE, nullptr) == -1) {
        throw std::system_error {
          errno, std::system_category(), "failed to make root mount private"
        };
      }

      std::vector<path_t> ro_mount_targets;

      // 1. Bind all requested directories/files into host_root_dir_
      for (const auto &src : normalized_binds) {
        const auto dst = host_root_dir_ / detail::to_relative(src);

        if (is_directory(src)) {
          create_directories(dst);
        } else {
          create_directories(dst.parent_path());
          std::ofstream {dst};
        }

        if (::mount(src.c_str(), dst.c_str(), nullptr, MS_BIND | MS_REC, nullptr) == -1) {
          throw std::system_error {
            errno, std::system_category(), "failed to bind mount: " + src.string()
          };
        }
        ro_mount_targets.push_back(dst);
      }

      // 2. Setup mocks if any
      if (!mocks.empty()) {
        setup_mocks(mocks, normalized_binds, ro_mount_targets);
      }

      // 3. Remount all bound paths read-only
      for (const auto &dst : ro_mount_targets) {
        if (::mount(nullptr, dst.c_str(), nullptr,
                    MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr) == -1) {
          throw std::system_error {
            errno, std::system_category(),
            "failed to remount read-only: " + dst.string()
          };
        }
      }

      // 4. Enter chroot
      if (::chdir(host_root_dir_.c_str()) == -1) {
        throw std::system_error {
          errno, std::system_category(),
          "failed to chdir to new root: " + host_root_dir_.string()
        };
      }

      if (::chroot(".") == -1) {
        throw std::system_error {
          errno, std::system_category(),
          "failed to chroot to directory: " + host_root_dir_.string()
        };
      }

      // 5. Start IPC server thread if mocks are registered
      if (!mocks_.empty()) {
        start_ipc_server();
      }
    } catch (...) {
      stop_ipc_server();
      if (::fchdir(old_root_) == 0) {
        if (::chroot(".") != 0) {}
      }
      if (::setns(old_mnt_ns_, CLONE_NEWNS) != 0) {}
      if (::fchdir(old_cwd_) != 0) {}
      std::error_code ec;
      remove_all(host_root_dir_, ec);
      throw;
    }
  }

  void setup_mocks(const std::vector<MockEntry> &mocks,
                   const std::vector<path_t> &normalized_binds,
                   std::vector<path_t> &ro_mount_targets) {
    const path_t self_exe = read_symlink("/proc/self/exe");

    // Ensure shared libraries required by /proc/self/exe (e.g. libgtest, libstdc++, ld.so)
    // are available inside the scoped root filesystem even if their parent directories
    // were not explicitly bound by the caller.
    for (const auto &lib_path : detail::collect_self_mapped_files()) {
      if (detail::is_subpath_of_any(lib_path, normalized_binds)) {
        continue;
      }
      const path_t dst_lib = host_root_dir_ / detail::to_relative(lib_path);
      if (exists(dst_lib)) continue;

      create_directories(dst_lib.parent_path());
      {
        std::ofstream {dst_lib};
      }
      if (::mount(lib_path.c_str(), dst_lib.c_str(), nullptr, MS_BIND, nullptr) == -1) {
        throw std::system_error {
          errno, std::system_category(),
          "failed to bind shared library for mock stub: " + lib_path.string()
        };
      }
      ro_mount_targets.push_back(dst_lib);
    }

    // Create IPC directory inside new root and bind a listening UNIX domain socket
    const path_t ipc_dir = host_root_dir_ / ".mock_ipc";
    create_directories(ipc_dir);

    int listen_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd == -1) {
      throw std::system_error {
        errno, std::system_category(), "failed to create mock IPC socket"
      };
    }
    if (int high_fd = ::fcntl(listen_fd, F_DUPFD_CLOEXEC, 1000); high_fd != -1) {
      ::close(listen_fd);
      listen_fd = high_fd;
    }
    listen_sock_ = std::make_unique<unique_fd>(listen_fd);

    if (::chdir(ipc_dir.c_str()) == -1) {
      throw std::system_error {
        errno, std::system_category(), "failed to chdir to .mock_ipc"
      };
    }
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "sock", sizeof(addr.sun_path) - 1);
    if (::bind(*listen_sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
      const int err = errno;
      if (::fchdir(old_cwd_) != 0) {}
      throw std::system_error {
        err, std::system_category(), "failed to bind mock IPC socket"
      };
    }
    if (::fchdir(old_cwd_) == -1) {
      throw std::system_error {
        errno, std::system_category(), "failed to restore cwd after binding socket"
      };
    }

    if (::listen(*listen_sock_, 32) == -1) {
      throw std::system_error {
        errno, std::system_category(), "failed to listen on mock IPC socket"
      };
    }

    int pfd[2];
    if (::pipe2(pfd, O_CLOEXEC) == -1) {
      throw std::system_error {
        errno, std::system_category(), "failed to create shutdown pipe"
      };
    }
    stop_pipe_r_ = std::make_unique<unique_fd>(pfd[0]);
    stop_pipe_w_ = std::make_unique<unique_fd>(pfd[1]);

    for (const auto &entry : mocks) {
      const auto norm_target = detail::to_normalized_abs(entry.target);
      const auto rel_target = detail::to_relative(norm_target);
      const path_t target_in_root = host_root_dir_ / rel_target;

      // Target must already exist inside the scoped filesystem (via prior bind)
      if (!exists(target_in_root)) {
        throw std::invalid_argument {
          "mock target does not exist inside scoped filesystem: " + norm_target.string()
        };
      }

      // Preserve the original executable inside /.mock_orig/<rel_target>
      const path_t orig_chroot_path = path_t {"/.mock_orig"} / rel_target;
      const path_t orig_in_root = host_root_dir_ / ".mock_orig" / rel_target;
      create_directories(orig_in_root.parent_path());
      {
        std::ofstream {orig_in_root};
      }

      if (::mount(target_in_root.c_str(), orig_in_root.c_str(),
                  nullptr, MS_BIND, nullptr) == -1) {
        throw std::system_error {
          errno, std::system_category(),
          "failed to bind original executable for mock: " + norm_target.string()
        };
      }
      ro_mount_targets.push_back(orig_in_root);

      // Overmount the target executable with /proc/self/exe (which has mock_stub_entry in .init_array)
      if (::mount(self_exe.c_str(), target_in_root.c_str(),
                  nullptr, MS_BIND, nullptr) == -1) {
        throw std::system_error {
          errno, std::system_category(),
          "failed to mount mock stub onto: " + norm_target.string()
        };
      }
      ro_mount_targets.push_back(target_in_root);

      mocks_.emplace(norm_target, entry.builder(norm_target, orig_chroot_path));
    }

    // Bind-mount ipc_dir onto itself so it becomes a mount point and can be remounted read-only
    if (::mount(ipc_dir.c_str(), ipc_dir.c_str(), nullptr, MS_BIND, nullptr) == -1) {
      throw std::system_error {
        errno, std::system_category(), "failed to self-bind .mock_ipc directory"
      };
    }
    ro_mount_targets.push_back(ipc_dir);
  }

  void start_ipc_server() {
    server_running_ = true;
    ipc_thread_ = std::thread([this] { ipc_loop(); });
  }

  void stop_ipc_server() noexcept {
    if (server_running_.exchange(false)) {
      if (stop_pipe_w_) {
        const char c = 'q';
        if (::write(*stop_pipe_w_, &c, 1) < 0) {}
      }
      if (ipc_thread_.joinable()) {
        ipc_thread_.join();
      }
    }
  }

  void ipc_loop() {
    pollfd pfds[2] {
      {.fd = *listen_sock_, .events = POLLIN, .revents = 0},
      {.fd = *stop_pipe_r_, .events = POLLIN, .revents = 0},
    };

    while (server_running_) {
      int r = ::poll(pfds, 2, -1);
      if (r < 0) {
        if (errno == EINTR) continue;
        break;
      }
      if (pfds[1].revents & POLLIN) {
        break;
      }
      if (pfds[0].revents & POLLIN) {
        int client_fd = ::accept4(*listen_sock_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd >= 0) {
          handle_client(unique_fd {client_fd});
        }
      }
    }
  }

  void handle_client(unique_fd client_fd) {
    int argc = 0;
    iovec iov {&argc, sizeof(argc)};
    union {
      char buf[CMSG_SPACE(sizeof(int) * 3)];
      cmsghdr align;
    } u {};

    msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    ssize_t n = ::recvmsg(client_fd, &msg, MSG_CMSG_CLOEXEC);
    if (n != sizeof(argc)) return;

    int passed_fds[3] {-1, -1, -1};
    for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        std::memcpy(passed_fds, CMSG_DATA(cmsg), sizeof(passed_fds));
        break;
      }
    }

    unique_fd in_fd {passed_fds[0]};
    unique_fd out_fd {passed_fds[1]};
    unique_fd err_fd {passed_fds[2]};

    std::vector<std::string> argv;
    argv.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
      std::string arg;
      char ch = 0;
      while (true) {
        ssize_t r = ::read(client_fd, &ch, 1);
        if (r <= 0 || ch == '\0') break;
        arg.push_back(ch);
      }
      argv.push_back(std::move(arg));
    }

    detail::StubResponse resp {};
    resp.action = 0;
    resp.code = 127;

    ExecutableMock *matched_mock = nullptr;
    if (!argv.empty()) {
      const path_t invoked {argv[0]};
      if (invoked.is_absolute()) {
        auto it = mocks_.find(invoked.lexically_normal());
        if (it != mocks_.end()) matched_mock = it->second.get();
      }
      if (!matched_mock) {
        for (auto &[k, v] : mocks_) {
          if (k.filename() == invoked.filename()) {
            matched_mock = v.get();
            break;
          }
        }
      }
    }

    if (matched_mock) {
      matched_mock->reset_invocation_state();
      const int code = matched_mock->execute(argv, in_fd, out_fd, err_fd);
      if (matched_mock->is_exec_original_requested()) {
        resp.action = 1;
        resp.code = 0;
        std::strncpy(resp.orig_path,
                     matched_mock->original_path().c_str(),
                     sizeof(resp.orig_path) - 1);
      } else {
        resp.action = 0;
        resp.code = code;
      }
    }

    (void)detail::write_all(client_fd, &resp, sizeof(resp));
  }

 private:
  path_t host_root_dir_;
  unique_fd old_root_;
  unique_fd old_cwd_;
  unique_fd old_mnt_ns_;

  std::unique_ptr<unique_fd> listen_sock_;
  std::unique_ptr<unique_fd> stop_pipe_r_;
  std::unique_ptr<unique_fd> stop_pipe_w_;
  std::atomic<bool> server_running_ {false};
  std::thread ipc_thread_;

  std::map<path_t, std::unique_ptr<ExecutableMock>> mocks_;
};

class RootFilesystemFactory {
 public:
  explicit RootFilesystemFactory(path_t root_dir) :
    root_dir_ {std::move(root_dir)}
  {}

  RootFilesystemFactory &bind(const path_t &p) {
    binds_.push_back(p);
    return *this;
  }

  template <template <typename> class MockType = ::testing::StrictMock>
  RootFilesystemFactory &mock(const path_t &p) {
    mocks_.push_back(ScopedRootFilesystem::MockEntry {
      .target = p,
      .builder = [](path_t target, path_t orig) -> std::unique_ptr<ExecutableMock> {
        return std::make_unique<MockType<ExecutableMock>>(std::move(target), std::move(orig));
      }
    });
    return *this;
  }

  [[nodiscard]]
  ScopedRootFilesystem create() const {
    return ScopedRootFilesystem {root_dir_, binds_, mocks_};
  }

 private:
  path_t root_dir_;
  std::vector<path_t> binds_;
  std::vector<ScopedRootFilesystem::MockEntry> mocks_;
};

} // namespace gh4ck3r::filesystem
