#pragma once
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <csignal>
#include <gh4ck3r/defer.hh>

extern "C" {
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/pidfd.h>
#include <sys/syscall.h>

extern "C" char **environ;
}

namespace gh4ck3r::process {

using path_t = std::filesystem::path;

inline const path_t proc_dir {"/proc"};

class Argv : protected std::vector<std::string> {
  using argv_t = char *const *;
  mutable std::vector<const char *> ptrs_;

 public:
  Argv(std::initializer_list<value_type> init) : vector(init) {}

  using vector::front;
  using vector::size;
  using vector::emplace_back;

  inline operator argv_t() const {
    ptrs_.clear();
    ptrs_.reserve(size());
    std::transform(begin(), end(), std::back_inserter(ptrs_),
                   [] (auto &s) { return s.data(); });
    ptrs_.push_back(nullptr);

    return const_cast<argv_t>(ptrs_.data());
  }
};

class Env : protected std::map<std::string, std::string> {
  using envp_t = char *const *;
  mutable std::vector<std::string> buf_;
  mutable std::vector<const char *> ptrs_;

 public:
  Env(char *const * envp = environ) {
    if (!envp || !*envp) return;

    std::string_view env {*envp};
    while (!env.empty()) {
      const auto pos = env.find('=');
      if (pos == env.npos) [[unlikely]]
        throw std::invalid_argument {"Env entry should have '='" + std::string{env}};

      insert_or_assign(std::string{env.substr(0, pos)}, std::string{env.substr(pos + 1)});

      env = (*++envp) ? *envp : std::string_view{};
    }
  }

  using map::begin;
  using map::end;
  using map::clear;
  using map::size;
  using map::operator[];

  inline operator envp_t() const {
    buf_.clear();

    const auto siz = size();
    buf_.reserve(siz);
    std::transform(begin(), end(), std::back_inserter(buf_),
                   [] (auto &kv) { return kv.first + '=' + kv.second; });

    ptrs_.clear();
    ptrs_.reserve(siz + 1);
    std::transform(buf_.begin(), buf_.end(), std::back_inserter(ptrs_),
                   [] (auto &s) { return s.data(); });
    ptrs_.emplace_back(nullptr);

    return const_cast<envp_t>(ptrs_.data());
  }
};

inline path_t operator/(const path_t &path, pid_t pid) {
  return path / std::to_string(pid);
}

inline bool is_executable(const path_t &path) {
  return ::faccessat(AT_FDCWD, path.c_str(), F_OK | X_OK, AT_EACCESS) == 0;
}

inline bool exists(pid_t pid) {
  if (pid <= 0) throw std::invalid_argument {
    "pid to check existence is not positive: " + std::to_string(pid)};
  return (kill(pid, 0) == 0) || (errno != ESRCH);
}

inline void set_stdio(int in_fd, int out_fd, int err_fd) {
  const std::array fd_map {
    std::make_pair(in_fd, STDIN_FILENO),
    std::make_pair(out_fd, STDOUT_FILENO),
    std::make_pair(err_fd, STDERR_FILENO),
  };

  std::vector<int> close_fds;
  for (const auto &[from, to] : fd_map) {
    if (from < 0) {
      close_fds.emplace_back(to);
      continue;
    }
    if (from == to) continue;

    if (dup2(from, to) == -1) [[unlikely]] {
      throw std::system_error { errno, std::system_category(),
        "failed to set stdio: "
          + std::to_string(from) + " -> " + std::to_string(to)
      };
    }

    if (STDERR_FILENO < from &&
        std::find(close_fds.begin(), close_fds.end(), from) == close_fds.end()) {
      close_fds.emplace_back(from);
    }
  }

  for (const auto fd : close_fds) ::close(fd);
}

inline std::string nameof(pid_t pid) {
  std::string name;
  std::getline(std::ifstream { proc_dir / pid / "comm" }, name);
  return name;
}

inline Argv cmdof(pid_t pid) {
  constexpr char delim = 0x00;

  Argv argv{};

  std::ifstream cmdline { proc_dir / pid / "cmdline" };
  for (std::string arg; std::getline(cmdline, arg, delim);)
    argv.emplace_back(arg);

  return argv;
}

inline path_t execof(pid_t pid)
{
  path_t path {};
  try {
    path = read_symlink(proc_dir / pid / "exe");
  } catch (const std::system_error &e) {
    // This could happen if the proc entry is about kernel thread or thread, its
    // parent is already terminated.
    if (e.code() != std::errc::no_such_file_or_directory) throw;
  }
  return path;
}

[[noreturn]]
inline void execve(const path_t &file,
                   const Argv &argv,
                   const Env &envp = environ)
{
  if (!is_executable(file))
    throw std::invalid_argument {file.string() + " is not an executable"};

  if (::execve(file.c_str(), argv, envp) == -1)
    throw std::runtime_error {"failed to execute " + file.string()};

  throw std::runtime_error {"This MUST never be reached"};
}

// https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html#tag_19_22_16
enum class exit_code : int {
  success             = EXIT_SUCCESS,
  failure             = EXIT_FAILURE,
  // A file to be executed was found, but it was not an executable utility.
  cannot_execute      = 126,
  // A utility to be executed was not found.
  command_not_found   = 127,
  // An unrecoverable read error was detected by the shell while reading
  // commands, except from the file operand of the dot special built-in.
  invalid             = 128,
  // A command was interrupted by a signal.
  signaled            = 129,
  out_of_range        = 255,
};

inline void exit(exit_code ec) { std::exit(static_cast<int>(ec)); }

/// This is a just simple wrapper around fork/exec.
[[nodiscard("A caller is responsible for waiting for the child process")]]
inline pid_t execute(const int stdin_fd,
                     const int stdout_fd,
                     const int stderr_fd,
                     const path_t &file,
                     const Argv &argv = {},
                     const Env  &envp = environ)
{
  if (!is_executable(file))
    throw std::invalid_argument {file.string() + " is not an executable"};

  const auto pid {fork()};
  if (pid == 0) try {
    set_stdio(stdin_fd, stdout_fd, stderr_fd);
    execve(file, argv.size() ? argv : Argv {file.c_str()}, envp);
  } catch (const std::exception &e) {
    if (0 < stderr_fd) {
      ::dprintf(STDERR_FILENO, "exception while execute child process: %s\n", e.what());
    }
    ::_exit(static_cast<int>(exit_code::invalid));
  } catch (...) {
    if (0 < stderr_fd) {
      ::dprintf(STDERR_FILENO, "unknown exception while execute child process\n");
    }
    ::_exit(static_cast<int>(exit_code::invalid));
  } else if (pid < 0) [[unlikely]] {
    throw std::system_error {errno, std::system_category(),
        "failed to fork a new process"};
  }

  return pid;
}

namespace detail {
template <typename T>
std::string stringify(T&& val) {
  if constexpr (std::is_convertible_v<T, std::string>) {
    return std::forward<T>(val);
  } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
    return std::to_string(std::forward<T>(val));
  } else {
    std::stringstream ss;
    ss << std::forward<T>(val);
    return ss.str();
  }
}
} // namespace detail

template <typename FIRST, typename...REST>
requires (!std::is_same_v<Argv, std::decay_t<FIRST>>)
[[nodiscard("A caller is responsible for waiting for the child process")]]
pid_t execute(const int stdin_fd,
              const int stdout_fd,
              const int stderr_fd,
              const path_t &file,
              FIRST&& first,
              REST&&...rest) {
  return execute(stdin_fd, stdout_fd, stderr_fd,
                 file,
                 Argv {
                  file.c_str(),
                  detail::stringify(std::forward<FIRST>(first)),
                  (detail::stringify(std::forward<REST>(rest)))...
                 });
}

template <class...ARGS>
[[nodiscard("A caller is responsible for waiting for the child process")]]
inline pid_t execute(const path_t &file, ARGS&&...args) {
  return execute(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
                 file, std::forward<ARGS>(args)...);
}

class timeout_error : public std::runtime_error {
 public:
  timeout_error() = delete;
  inline timeout_error(const pid_t &id, const std::chrono::nanoseconds &d) :
    std::runtime_error {
      "process(" + std::to_string(id) + ") is not terminated normally within given duration "
      + detail::stringify(d)
    }, pid_(id), duration_(d)
  {}

  inline pid_t pid() const { return pid_; }
  inline std::chrono::nanoseconds duration() const { return duration_; }

 private:
  const pid_t pid_;
  const std::chrono::nanoseconds duration_;
};

inline int wait(const pid_t pid)
{
  const auto pid_fd {pidfd_open(pid, 0)};
  if (pid_fd == -1) [[unlikely]] throw std::system_error {
    errno, std::system_category(), "failed to open pidfd" };

  auto ec {static_cast<int>(exit_code::out_of_range)};
  if (siginfo_t siginfo; ::waitid(P_PIDFD, pid_fd, &siginfo, WEXITED) == 0) {
    ec = siginfo.si_code == CLD_EXITED ? siginfo.si_status :
      static_cast<int>(exit_code::signaled) + siginfo.si_status;
  }

  ::close(pid_fd);
  return ec;
}

inline int wait_for(const pid_t pid, const std::chrono::nanoseconds &d)
{
  if (pid <= 0) [[unlikely]] throw std::invalid_argument {
    "pid should be positive to specify exact one process: " + std::to_string(pid)};
  else if (d < d.zero()) [[unlikely]] throw std::invalid_argument {
      "duration must be positive for waiting process termination: " + std::to_string(pid)};

  const auto pid_fd {pidfd_open(pid, 0)};
  if (pid_fd == -1) [[unlikely]] {
    if (errno == ESRCH) return wait(pid);
    throw std::system_error {errno, std::system_category(), "failed to open pidfd"};
  }
  gh4ck3r::Defer close_fd {[&] { ::close(pid_fd); }};

  struct pollfd pfd {
    .fd = static_cast<int>(pid_fd),
    .events = POLLIN,
    .revents = 0,
  };
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();

  const auto ret = ::poll(&pfd, 1, ms);
  if (ret == 0) {
    throw timeout_error {pid, d};
  } else if (ret < 0) {
    throw std::system_error {errno, std::system_category(), "failed to poll pidfd"};
  }

  return wait(pid);
}

inline pid_t ppidof(const pid_t pid)
{
  if (pid == getpid()) return ::getppid();

  std::string line;
  if (!std::getline(std::ifstream {proc_dir / pid / "stat"}, line))
    [[unlikely]] throw std::runtime_error {"failed to load /proc/<pid>/stat"};

  char state;
  pid_t ppid = {};
  // man proc_pid_stat
  const auto pos = line.find_last_of(')');
  if (pos == std::string::npos) [[unlikely]] throw std::runtime_error {
    "invalid /proc/" + std::to_string(pid) + "/stat format: closing parenthesis not found"};

  std::istringstream {line.substr(pos + 1)} >> state >> ppid;
  return ppid;
}

} // namespace gh4ck3r::process
