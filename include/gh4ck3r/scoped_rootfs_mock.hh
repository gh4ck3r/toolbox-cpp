#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <gh4ck3r/file.hh>
#include <gmock/gmock.h>
#include "scoped_rootfs_binder.hh"

#ifndef __GLIBC__
#error "scoped_rootfs.hh requires glibc (.init_array argc/argv/envp and AT_EXECFN)"
#endif

namespace gh4ck3r::filesystem::scoped_rootfs {

// Wire protocol shared by the mock stub (child process) and the server (test process).
// The stub runs before C++ dynamic initialization, so everything in this namespace
// must use only libc/syscalls, trivial types and constant-initialized objects:
// no std::string/std::filesystem::path, no heap allocation, no exceptions.
namespace detail::mock_ipc {

inline constexpr const char *ipc_dir = "/.mock_ipc";
inline constexpr const char *stub_path = "/.mock_ipc/stub";
inline constexpr const char *originals_dir = "/.mock_orig";
inline constexpr sockaddr_un ipc_addr {
  .sun_family = AF_UNIX,
  .sun_path = "/.mock_ipc/sock",
};

inline constexpr int transport_error = 125;
inline constexpr int passthrough = 256;
inline constexpr uint32_t protocol_magic = 0x52465332;  // "RFS2"
inline constexpr uint32_t max_arguments = 1024;
inline constexpr uint32_t max_string = 64 * 1024;
inline constexpr size_t max_payload = 1024 * 1024;
inline constexpr std::chrono::seconds timeout {10};

struct RequestHeader {
  uint32_t magic;
  uint32_t argc;
  uint32_t command_size;
};

struct Response {
  uint32_t magic = protocol_magic;
  int code = transport_error;
};

template <typename Op>
inline bool transfer_all(char *bytes, std::size_t size, Op &&op) noexcept {
  while (size) {
    const auto count = op(bytes, size);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    bytes += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

inline bool send_all(int fd, const void *data, std::size_t size) noexcept {
  return transfer_all(
    const_cast<char *>(static_cast<const char *>(data)),
    size,
    [fd](const char *buf, std::size_t len) {
      return ::send(fd, buf, len, MSG_NOSIGNAL);
    });
}

inline bool read_all(int fd, void *data, std::size_t size) noexcept {
  return transfer_all(
    static_cast<char *>(data),
    size,
    [fd](char *buf, std::size_t len) {
      return ::read(fd, buf, len);
    });
}

inline bool set_timeouts(int fd) noexcept {
  const timeval tv {timeout.count(), 0};
  for (const auto optname : {SO_RCVTIMEO, SO_SNDTIMEO}) {
    if (::setsockopt(fd, SOL_SOCKET, optname, &tv, sizeof(tv)) != 0)
      return false;
  }
  return true;
}

} // namespace detail::mock_ipc

// Client side executed inside the mocked child process before main().
// The same constraints as detail::mock_ipc apply.
namespace detail::mock_stub {

using namespace mock_ipc;

[[noreturn]]
inline void fail() noexcept {
  constexpr std::string_view message {"mock command transport failed\n"};
  [[maybe_unused]]
  const auto r = ::write(STDERR_FILENO, message.data(), message.size());
  ::_exit(transport_error);
}

inline bool resolve_paths(auto &command, auto &original) noexcept {
  struct stat status {};
  if (::stat(originals_dir, &status) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) return false;
    fail();
  }
  if (!S_ISDIR(status.st_mode)) fail();

  const auto *executed = reinterpret_cast<const char *>(::getauxval(AT_EXECFN));
  if (!executed || !*executed || !::realpath(executed, command)) fail();

  const auto prefix = std::strlen(originals_dir);
  const auto size = std::strlen(command);
  if (prefix + size >= sizeof(original)) return false;
  std::memcpy(original, originals_dir, prefix);
  std::memcpy(original + prefix, command, size + 1);
  if (::lstat(original, &status) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) return false;
    fail();
  }
  if (!S_ISREG(status.st_mode)) fail();
  return true;
}

// Registration is identified by the execution path, not argv[0] or socket state.
__attribute__((constructor(101)))
inline void pre_main(int argc, char **argv, char **envp) noexcept {
  char command[PATH_MAX];
  char original[PATH_MAX];
  if (!resolve_paths(command, original)) return;
  if (argc <= 0 || static_cast<unsigned int>(argc) > max_arguments) fail();

  const int sock = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock < 0 || !set_timeouts(sock)) fail();
  if (::connect(sock, reinterpret_cast<const sockaddr *>(&ipc_addr),
                sizeof(ipc_addr)) != 0) fail();

  const auto command_size = static_cast<std::uint32_t>(std::strlen(command));
  RequestHeader header {protocol_magic, static_cast<std::uint32_t>(argc), command_size};
  iovec iov {&header, sizeof(header)};
  union {
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
  if (sent != sizeof(header) || !send_all(sock, command, command_size))
    fail();

  std::size_t total = command_size;
  for (int i = 0; i < argc; ++i) {
    const auto size = ::strnlen(argv[i], max_string + 1);
    if (size > max_string || (total += size) > max_payload) fail();
    const auto length = static_cast<std::uint32_t>(size);
    if (!send_all(sock, &length, sizeof(length)) ||
        !send_all(sock, argv[i], length)) fail();
  }
  Response response;
  if (!read_all(sock, &response, sizeof(response))) fail();
  ::close(sock);
  if (response.magic != protocol_magic) fail();
  if (response.code == passthrough) {
    ::execve(original, argv, envp);
    fail();
  }
  if (response.code < 0 || response.code > 255) fail();
  ::_exit(response.code);
}

} // namespace detail::mock_stub

class MockExecutable {
 public:
  static constexpr int passthrough = detail::mock_ipc::passthrough;

  explicit MockExecutable(path_t target, path_t original) :
    target_path_ {std::move(target)}, orig_chroot_path_ {std::move(original)}
  {
    using ::testing::_;
    ON_CALL(*this, execute(_, _, _, _))
      .WillByDefault(::testing::Invoke(this, &MockExecutable::execute_original));
  }
  virtual ~MockExecutable() noexcept = default;
  MOCK_METHOD(int, execute, (const std::vector<std::string> &argv,
                            int stdin_fd, int stdout_fd, int stderr_fd));
  int execute_original(const std::vector<std::string> &, int, int, int) noexcept {
    return passthrough;
  }
  const path_t &path() const noexcept { return target_path_; }
  const path_t &original_path() const noexcept { return orig_chroot_path_; }

 private:
  path_t target_path_;
  path_t orig_chroot_path_;
};

namespace detail {

class MockExecutableServer {
 public:
  MockExecutableServer() = default;
  MockExecutableServer(const MockExecutableServer &) = delete;
  MockExecutableServer &operator=(const MockExecutableServer &) = delete;

  ~MockExecutableServer() noexcept try {
    stop();
  } catch (...) {
    std::terminate();
  }

 protected:
  using mock_builder_t = std::function<std::unique_ptr<MockExecutable>(path_t, path_t)>;

  struct Entry {
    path_t target;
    mock_builder_t builder;
  };

  static constexpr const char *ipc_dir = mock_ipc::ipc_dir;
  static constexpr const char *stub_path = mock_ipc::stub_path;
  static constexpr const char *originals_dir = mock_ipc::originals_dir;

  void emplace(path_t target, std::unique_ptr<MockExecutable> executable) {
    mocks_.emplace(std::move(target), std::move(executable));
  }

  MockExecutable &mock(const path_t &executable) const {
    const auto found = mocks_.find(executable);
    if (found == mocks_.end())
      throw std::out_of_range {"mock not found for path: " + executable.string()};
    return *found->second;
  }

  void start() {
    using mock_ipc::ipc_addr;
    if (mocks_.empty()) return;

    listen_sock_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_sock_ < 0) throw system_error {"create mock listener"};
    if (::bind(listen_sock_, reinterpret_cast<const sockaddr *>(&ipc_addr), sizeof(ipc_addr)) != 0)
      throw system_error {"bind mock listener"};
    if (::chmod(ipc_addr.sun_path, 0600) != 0 || ::listen(listen_sock_, 32) != 0)
      throw system_error {"prepare mock listener"};

    stopping_.store(false, std::memory_order_relaxed);
    thread_ = std::thread {&MockExecutableServer::ipc_loop, this};
  }

  void stop() {
    if (thread_.joinable()) {
      stopping_.store(true, std::memory_order_release);
      ::shutdown(listen_sock_, SHUT_RDWR);
      thread_.join();
    }
    if (listen_sock_ != -1) {
      ::close(std::exchange(listen_sock_, -1));
      if (::unlink(mock_ipc::ipc_addr.sun_path) != 0 && errno != ENOENT)
        throw system_error {"remove mock socket"};
    }
  }

  std::vector<std::exception_ptr> take_worker_errors() {
    std::lock_guard<std::mutex> lock {worker_mutex_};
    std::vector<std::exception_ptr> errors;
    errors.swap(worker_errors_);
    return errors;
  }

 private:
  using ExecutableMap = std::map<path_t, std::unique_ptr<MockExecutable>>;

  struct Request {
    std::string command;
    std::vector<std::string> argv;
    std::array<int, 3> fds {-1, -1, -1};
  };

  void record_worker_error(std::exception_ptr error) noexcept {
    std::lock_guard<std::mutex> lock {worker_mutex_};
    worker_errors_.push_back(std::move(error));
  }

  void ipc_loop() noexcept {
    std::vector<std::thread> clients;
    try {
      for (;;) {
        const int fd = ::accept4(listen_sock_, nullptr, nullptr, SOCK_CLOEXEC);
        if (stopping_.load(std::memory_order_acquire)) break;
        if (fd < 0) {
          if (errno == EINTR) continue;
          throw system_error {"accept mock client"};
        }
        clients.emplace_back([this, fd]() noexcept {
          try {
            handle_client(fd);
          } catch (...) {
            record_worker_error(std::current_exception());
          }
          ::close(fd);
        });
      }
    } catch (...) {
      record_worker_error(std::current_exception());
    }
    for (auto &client : clients)
      if (client.joinable()) client.join();
  }

  static void close_fds(const std::array<int, 3> &fds) noexcept {
    for (const int fd : fds)
      if (fd != -1) ::close(fd);
  }

  static Request receive_request(int client) {
    using namespace mock_ipc;
    if (!set_timeouts(client))
      throw system_error {"set mock IPC timeout"};
    RequestHeader header {};
    iovec iov {&header, sizeof(header)};
    union {
      char bytes[CMSG_SPACE(sizeof(int) * 3)];
      cmsghdr alignment;
    } control {};
    msghdr message {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = control.bytes,
      .msg_controllen = sizeof(control.bytes),
    };
    ssize_t count;
    do {
      count = ::recvmsg(client, &message, MSG_CMSG_CLOEXEC);
    } while (count < 0 && errno == EINTR);

    Request req;
    if (count > 0) {
      if (auto *cmsg = CMSG_FIRSTHDR(&message);
          cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        const auto n = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        std::memcpy(req.fds.data(), CMSG_DATA(cmsg), n * sizeof(int));
      }
    }
    try {
      if (count != sizeof(header) || (message.msg_flags & MSG_CTRUNC) || req.fds[2] == -1)
        throw std::runtime_error {"invalid mock IPC descriptors"};
      if (header.magic != protocol_magic ||
          header.command_size == 0 || header.command_size >= PATH_MAX ||
          header.argc == 0 || header.argc > max_arguments)
        throw std::runtime_error {"invalid mock request header"};

      req.command.resize(header.command_size);
      if (!read_all(client, req.command.data(), req.command.size()) ||
          req.command.front() != '/' || req.command.find('\0') != std::string::npos)
        throw std::runtime_error {"invalid mock command path"};

      std::size_t total = header.command_size;
      for (std::uint32_t i = 0; i < header.argc; ++i) {
        std::uint32_t length = 0;
        if (!read_all(client, &length, sizeof(length)) ||
            length > max_string || (total += length) > max_payload)
          throw std::runtime_error {"invalid mock argument size"};
        std::string argument(length, '\0');
        if (!read_all(client, argument.data(), argument.size()) ||
            argument.find('\0') != std::string::npos)
          throw std::runtime_error {"invalid mock argument"};
        req.argv.push_back(std::move(argument));
      }
      return req;
    } catch (...) {
      close_fds(req.fds);
      throw;
    }
  }

  void handle_client(int client) {
    using mock_ipc::Response;
    using mock_ipc::send_all;
    Request req;
    try {
      req = receive_request(client);
      const auto found = mocks_.find(req.command);
      if (found == mocks_.end())
        throw std::runtime_error {"unregistered mock command: " + req.command};

      auto &mock = *found->second;
      req.argv.front() = mock.path().string();

      const auto code = mock.execute(req.argv, req.fds[0], req.fds[1], req.fds[2]);
      if (code != MockExecutable::passthrough && (code < 0 || code > 255))
        throw std::runtime_error {"invalid mock exit status"};

      Response response {
        .magic = mock_ipc::protocol_magic,
        .code = code,
      };
      if (!send_all(client, &response, sizeof(response)))
        throw system_error {"send mock response"};
    } catch (...) {
      close_fds(req.fds);
      const Response error;
      if (!send_all(client, &error, sizeof(error)))
        std::fprintf(stderr, "failed to send mock error response: %s\n", std::strerror(errno));
      throw;
    }
    close_fds(req.fds);
  }

  ExecutableMap mocks_;
  int listen_sock_ = -1;
  std::atomic_bool stopping_ {false};
  std::thread thread_;
  std::mutex worker_mutex_;
  std::vector<std::exception_ptr> worker_errors_;
};

} // namespace detail
} // namespace gh4ck3r::filesystem::scoped_rootfs
