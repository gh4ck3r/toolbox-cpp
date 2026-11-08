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
#include <sys/uio.h>
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

inline constexpr const std::string_view originals_dir {"/.mock_orig"};
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

struct StdStream {
  int stdin;
  int stdout;
  int stderr;
};

template <auto FN, typename...ARGS>
inline bool transfer(int fd, void *bytes, size_t len, ARGS&&...args) noexcept
{
  auto p = reinterpret_cast<char*>(bytes);
  while (len) {
    const auto cnt = FN(fd, p, len, std::forward<ARGS>(args)...);
    if (cnt < 0 && errno == EINTR) continue;
    if (cnt <= 0) return false;

    p += cnt;
    len -= cnt;
  }
  return true;
}

inline bool send_all(int fd, const void *data, std::size_t size) noexcept {
  return transfer<::send>(fd, const_cast<void*>(data), size, MSG_NOSIGNAL);
}

inline bool read_all(int fd, void *data, std::size_t size) noexcept {
  return transfer<::read>(fd, data, size);
}

inline bool set_timeouts(int fd) noexcept {
  const timeval tv {timeout.count(), 0};
  for (const auto optname : {SO_RCVTIMEO, SO_SNDTIMEO}) {
    if (::setsockopt(fd, SOL_SOCKET, optname, &tv, sizeof(tv)) != 0)
      return false;
  }
  return true;
}

template <typename T>
class CMSG {
  alignas(cmsghdr) std::byte buffer[CMSG_SPACE(sizeof(T))];
 public:
  CMSG() { std::memset(buffer, 0, sizeof(buffer)); }
  explicit CMSG(const T& data) : CMSG {} {
    auto hdr = reinterpret_cast<cmsghdr*>(buffer);
    hdr->cmsg_len = CMSG_LEN(sizeof(T));
    hdr->cmsg_level = SOL_SOCKET;
    hdr->cmsg_type = SCM_RIGHTS;

    std::memcpy(CMSG_DATA(hdr), &data, sizeof(T));
  }

  operator void*() { return buffer; }
  size_t size() const { return sizeof(buffer); }
};

template <typename Payload, typename ControlData>
struct MsgHdr : msghdr {
  iovec iov;
  MsgHdr(const Payload &payload, CMSG<ControlData> &control) : msghdr {} {
    iov = {const_cast<Payload*>(&payload), sizeof(Payload)};
    msg_iov = &iov;
    msg_iovlen = 1;
    msg_control = control;
    msg_controllen = control.size();
  };
};

[[noreturn]]
inline void fail(const std::string_view &msg) noexcept {
  const std::array<iovec, 2> data {{
    {const_cast<char*>(msg.data()), msg.size()},
    {const_cast<char*>("\n"), 1},
  }};

  [[maybe_unused]]
  auto r = ::writev(STDERR_FILENO, data.data(), data.size());
  ::_exit(transport_error);
}

inline int connect() {
  const int sock = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock < 0 || !set_timeouts(sock))
    fail("failed to set timeout for the protocol");
  if (::connect(sock,
                reinterpret_cast<const sockaddr *>(&ipc_addr),
                sizeof(ipc_addr)) != 0)
    fail("failed to connect to the IPC server");
  return sock;
}

} // namespace detail::mock_ipc

// Client side executed inside the mocked child process before main().
// The same constraints as detail::mock_ipc apply.
namespace detail::mock_stub {

using namespace mock_ipc;

inline bool resolve_paths(auto &command, auto &original) noexcept {
  struct stat status {};
  if (::stat(originals_dir.data(), &status) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) return false;
    fail("Mock originals directory not found");
  }
  if (!S_ISDIR(status.st_mode)) fail("Mock originals directory is not a directory");

  if (auto executed = reinterpret_cast<const char *>(::getauxval(AT_EXECFN));
      (!executed || !*executed || !::realpath(executed, command)))
    fail("Mock original executable not identified");

  if (const auto n = std::snprintf(original,
                                   sizeof(original),
                                   "%s%s",
                                   originals_dir.data(),
                                   command);
      n < 0 || static_cast<ssize_t>(sizeof(original)) <= n)
    return false;

  if (::lstat(original, &status) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) return false;
    fail("Mock original executable not found");
  }
  if (!S_ISREG(status.st_mode))
    fail("Mock original executable found but not a regular file");
  return true;
}

inline void send_request(int sock, std::string_view command, int argc, char **argv) {
  const RequestHeader header {
    .magic = protocol_magic,
    .argc = static_cast<uint32_t>(argc),
    .command_size = static_cast<uint32_t>(command.size()),
  };

  CMSG<StdStream> fds {{STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}};
  MsgHdr msg {header, fds};

  if (::sendmsg(sock, &msg, MSG_NOSIGNAL) != sizeof(header))
    fail("to send file descriptors to the IPC server");

  if (!send_all(sock, command.data(), command.size()))
    fail("to send command command to the IPC server");

  size_t total = command.size();
  for (auto i = 0; i < argc; ++i) {
    const auto size = ::strnlen(argv[i], max_string + 1);
    if (size > max_string || (total += size) > max_payload)
      fail("arguments exceed maximum payload");

    const auto len = static_cast<uint32_t>(size);
    if (!send_all(sock, &len, sizeof(len)) || !send_all(sock, argv[i], len))
      fail("failed to send message to the IPC server");
  }
}

// Registration is identified by the execution path, not argv[0] or socket state.
__attribute__((constructor(101)))
inline void pre_main(int argc, char **argv, char **envp) noexcept {
  char command[PATH_MAX];
  char original[PATH_MAX];
  if (!resolve_paths(command, original)) return;
  if (argc <= 0 || static_cast<unsigned int>(argc) > max_arguments)
    fail("invalid argc");

  const int sock = connect();

  send_request(sock, command, argc, argv);

  Response response;
  if (!read_all(sock, &response, sizeof(response)))
    fail("failed to receive response from server");
  ::close(sock);

  if (response.magic != protocol_magic)
    fail("invalid response magic from the IPC server");
  if (response.code == passthrough) {
    ::execve(original, argv, envp);
    fail("failed to execute original executable");
  }
  if (response.code < 0 || response.code > 255)
    fail("invalid response code from the IPC server");
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

  static inline const path_t ipc_dir {"/.mock_ipc"};
  static inline const path_t stub_path {"/.mock_ipc/stub"};
  static inline const path_t originals_dir { mock_ipc::originals_dir };

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
    CMSG<StdStream> cmsg {};
    msghdr message {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = cmsg,
      .msg_controllen = cmsg.size(),
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
