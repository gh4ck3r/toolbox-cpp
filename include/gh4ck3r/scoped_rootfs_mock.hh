#pragma once
#include <array>
#include <atomic>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <gh4ck3r/file.hh>
#include <gmock/gmock.h>

#ifndef __GLIBC__
#error "scoped_rootfs.hh requires glibc (.init_array argc/argv/envp and AT_EXECFN)"
#endif

namespace gh4ck3r::filesystem::scoped_rootfs::mock {

constexpr sockaddr_un ipc_addr {
  .sun_family = AF_UNIX,
  .sun_path = "/.mock_ipc/sock",
};

inline constexpr const char *ipc_dir = "/.mock_ipc";
inline constexpr const char *stub_path = "/.mock_ipc/stub";
inline constexpr const char *originals_dir = "/.mock_orig";
inline constexpr int transport_error = 125;
inline constexpr std::uint32_t protocol_magic = 0x52465332;
inline constexpr std::uint32_t max_arguments = 1024;
inline constexpr std::uint32_t max_string = 64 * 1024;
inline constexpr std::size_t max_payload = 1024 * 1024;
inline constexpr int timeout_seconds = 10;

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

inline bool set_timeouts(int fd) noexcept {
  const timeval timeout {timeout_seconds, 0};

  for (const auto optname : {SO_RCVTIMEO, SO_SNDTIMEO}) {
    if (::setsockopt(fd, SOL_SOCKET, optname, &timeout, sizeof(timeout)) != 0)
      return false;
  }

  return true;
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
  if (::connect(sock, reinterpret_cast<const sockaddr *>(&ipc_addr),
                sizeof(ipc_addr)) != 0) stub_error();

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

class Executable;
class IpcServer;
using ExecutableMap = std::map<path_t, std::unique_ptr<Executable>>;

class Executable {
  using path_t = std::filesystem::path;
 public:
  explicit Executable(path_t target, path_t original) :
    target_path_ {std::move(target)}, orig_chroot_path_ {std::move(original)}
  {
    using ::testing::_;
    ON_CALL(*this, execute(_, _, _, _))
      .WillByDefault(::testing::Invoke(this, &Executable::execute_original));
  }
  virtual ~Executable() noexcept = default;
  MOCK_METHOD(int, execute, (const std::vector<std::string> &argv,
                            int stdin_fd, int stdout_fd, int stderr_fd));
  int execute_original(const std::vector<std::string> &, int, int, int) noexcept {
    exec_original_target_ = this;
    return 0;
  }
  const path_t &path() const noexcept { return target_path_; }
  const path_t &original_path() const noexcept { return orig_chroot_path_; }

 private:
  path_t target_path_;
  path_t orig_chroot_path_;
  inline static thread_local const Executable *exec_original_target_ = nullptr;
  friend class IpcServer;
};

[[noreturn]]
static inline void system_error(const std::string &operation, int error = errno) {
  throw std::system_error {error, std::system_category(), operation};
}

using mock_builder_t = std::function<std::unique_ptr<Executable>(path_t, path_t)>;
struct Entry {
  path_t target;
  mock_builder_t builder;
};

class IpcServer {
 public:
  IpcServer() = default;
  IpcServer(const IpcServer &) = delete;
  IpcServer &operator=(const IpcServer &) = delete;

  ~IpcServer() noexcept {
    if (thread_.joinable()) {
      try {
        stop();
      } catch (...) {
        std::terminate();
      }
    }
  }

  void emplace(path_t target, std::unique_ptr<Executable> executable) {
    if (thread_.joinable() || listen_sock_)
      throw std::logic_error {"mock IPC server already started"};
    mocks_.emplace(std::move(target), std::move(executable));
  }

  Executable &mock(const path_t &executable) const {
    const auto found = mocks_.find(executable);
    if (found == mocks_.end())
      throw std::out_of_range {"mock not found for path: " + executable.string()};
    return *found->second;
  }

  void start() {
    if (mocks_.empty()) return;
    if (thread_.joinable() || listen_sock_)
      throw std::logic_error {"mock IPC server already started"};

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) system_error("create mock listener");
    listen_sock_.emplace(fd);
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&ipc_addr), sizeof(ipc_addr)) != 0)
      system_error("bind mock listener");
    socket_owned_ = true;
    if (::chmod(ipc_addr.sun_path, 0600) != 0 || ::listen(fd, 32) != 0)
      system_error("prepare mock listener");

    int pipe[2];
    if (::pipe2(pipe, O_CLOEXEC | O_NONBLOCK) != 0)
      system_error("create mock shutdown pipe");
    stop_pipe_r_.emplace(pipe[0]);
    stop_pipe_w_.emplace(pipe[1]);
    std::packaged_task<void()> task {[this] { ipc_loop(); }};
    worker_result_ = task.get_future();
    thread_ = std::thread {std::move(task)};
  }

  void stop() {
    if (thread_.joinable()) {
      const char wakeup = 1;
      ssize_t written;
      do {
        written = ::write(*stop_pipe_w_, &wakeup, sizeof(wakeup));
      } while (written < 0 && errno == EINTR);
      if (written != sizeof(wakeup))
        system_error("stop mock listener");
      thread_.join();
    }
    listen_sock_.reset();
    stop_pipe_r_.reset();
    stop_pipe_w_.reset();
    if (socket_owned_) {
      if (::unlink(ipc_addr.sun_path) != 0 && errno != ENOENT)
        system_error("remove mock socket");
      socket_owned_ = false;
    }
  }

  std::vector<std::exception_ptr> take_worker_errors() {
    if (worker_result_.valid()) {
      try {
        worker_result_.get();
      } catch (...) {
        record_worker_error(std::current_exception());
      }
    }
    std::lock_guard<std::mutex> lock {worker_mutex_};
    std::vector<std::exception_ptr> errors;
    errors.swap(worker_errors_);
    return errors;
  }

 private:
  void handle_client(unique_fd client);

  struct ClientWorker {
    std::thread thread;
    std::atomic<bool> done {false};
  };

  void record_worker_error(std::exception_ptr error) noexcept {
    std::lock_guard<std::mutex> lock {worker_mutex_};
    worker_errors_.push_back(std::move(error));
  }

  void ipc_loop() {
    std::list<ClientWorker> clients;
    const auto reap_clients = [&](bool wait_all) noexcept {
      for (auto it = clients.begin(); it != clients.end();) {
        if (wait_all || it->done.load(std::memory_order_acquire)) {
          if (it->thread.joinable()) it->thread.join();
          it = clients.erase(it);
        } else {
          ++it;
        }
      }
    };
    try {
      for (;;) {
        reap_clients(false);
        std::array events {
          pollfd {*listen_sock_, POLLIN, 0},
          pollfd {*stop_pipe_r_, POLLIN, 0},
        };
        const int ready = ::poll(events.data(), events.size(), -1);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) system_error("poll mock listener");
        if (events[1].revents & POLLIN) break;
        if (!(events[0].revents & POLLIN))
          throw std::runtime_error {"unexpected mock listener event"};

        const int fd = ::accept4(*listen_sock_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0 && errno == EINTR) continue;
        if (fd < 0) system_error("accept mock client");
        auto &worker = clients.emplace_back();
        try {
          worker.thread = std::thread {[this, &worker, client = unique_fd {fd}]() mutable noexcept {
            try {
              handle_client(std::move(client));
            } catch (...) {
              record_worker_error(std::current_exception());
            }
            worker.done.store(true, std::memory_order_release);
          }};
        } catch (...) {
          clients.pop_back();
          throw;
        }
      }
    } catch (...) {
      reap_clients(true);
      record_worker_error(std::current_exception());
      return;
    }
    reap_clients(true);
  }

  ExecutableMap mocks_;
  std::optional<unique_fd> listen_sock_;
  std::optional<unique_fd> stop_pipe_r_;
  std::optional<unique_fd> stop_pipe_w_;
  std::thread thread_;
  std::future<void> worker_result_;
  std::mutex worker_mutex_;
  std::vector<std::exception_ptr> worker_errors_;
  bool socket_owned_ = false;
};

inline void IpcServer::handle_client(unique_fd client)
try {
  if (!set_timeouts(client))
    system_error("set mock IPC timeout");
  RequestHeader header {};
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
  if (!read_all(client, reinterpret_cast<char *>(&header) + count,
          sizeof(header) - static_cast<std::size_t>(count)))
    throw std::runtime_error {"incomplete mock request header"};
  if (header.magic != protocol_magic ||
      header.command_size == 0 || header.command_size >= PATH_MAX || header.argc == 0 ||
      header.argc > max_arguments)
    throw std::runtime_error {"invalid mock request header"};
  std::string command(header.command_size, '\0');
  if (!read_all(client, command.data(), command.size()) ||
      command.front() != '/' || command.find('\0') != std::string::npos)
    throw std::runtime_error {"invalid mock command path"};
  const auto found = mocks_.find(command);
  if (found == mocks_.end())
    throw std::runtime_error {"unregistered mock command: " + command};
  std::vector<std::string> argv;
  std::size_t total = header.command_size;
  for (std::uint32_t i = 0; i < header.argc; ++i) {
    std::uint32_t length = 0;
    if (!read_all(client, &length, sizeof(length)) ||
        length > max_string ||
        (total += length) > max_payload)
      throw std::runtime_error {"invalid mock argument size"};
    std::string argument(length, '\0');
    if (!read_all(client, argument.data(), argument.size()) ||
        argument.find('\0') != std::string::npos)
      throw std::runtime_error {"invalid mock argument"};
    argv.push_back(std::move(argument));
  }
  auto &mock = *found->second;
  argv.front() = mock.path().string();
  Executable::exec_original_target_ = nullptr;
  Response response;
  response.code = mock.execute(argv, passed.descriptors[0],
                                passed.descriptors[1], passed.descriptors[2]);
  if (Executable::exec_original_target_ == &mock && response.code == 0) {
    const auto original = mock.original_path().string();
    if (original.size() >= sizeof(response.original))
      throw std::runtime_error {"mock executable path is too long"};
    response.action = 1;
    std::memcpy(response.original, original.c_str(), original.size() + 1);
  } else if (response.code < 0 || response.code > 255) {
    throw std::runtime_error {"invalid mock exit status"};
  }
  if (!send_all(client, &response, sizeof(response)))
    system_error("send mock response");
} catch (...) {
  const Response error;
  if (!send_all(client, &error, sizeof(error)))
    std::fprintf(stderr, "failed to send mock error response: %s\n", std::strerror(errno));
  throw;
}

} // namespace gh4ck3r::filesystem::scoped_rootfs::mock
