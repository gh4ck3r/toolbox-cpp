#pragma once
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

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
    transform(begin(), end(), back_inserter(ptrs_),
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
    if (!envp) return;

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
    transform(begin(), end(), back_inserter(buf_),
              [] (auto &kv) { return kv.first + '=' + kv.second; });

    ptrs_.clear();
    ptrs_.reserve(siz + 1);
    transform(buf_.begin(), buf_.end(), back_inserter(ptrs_),
              [] (auto &s) { return s.data(); });
    ptrs_.emplace_back(nullptr);

    return const_cast<envp_t>(ptrs_.data());
  }
};

inline path_t operator/(const path_t &path, pid_t pid) {
  return path / std::to_string(pid);
}

inline bool is_executable(const path_t &path) {
  return access(path.c_str(), F_OK | X_OK) == 0;
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

  for (const auto &[from, to] : fd_map) {
    if (from < 0) close(to);
    else if (dup2(from, to) != -1) close(from);
    else throw std::system_error { errno, std::system_category(),
      "failed to set stdio: "
        + std::to_string(from) + " -> " + std::to_string(to)
    };
  }
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
    if (e.code().value() != ENOENT) throw;
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

/// This is a just simple wrapper around fork/exec.
[[nodiscard("A caller is responsible for waiting for the child process")]]
inline pid_t execute(const int stdin,
                     const int stdout,
                     const int stderr,
                     const path_t &file,
                     const Argv &argv = {},
                     const Env  &envp = environ)
{
  if (!is_executable(file))
    throw std::invalid_argument {file.string() + " is not an executable"};

  const auto pid {fork()};
  if (pid == 0) [[likely]] {
    set_stdio(stdin, stdout, stderr);
    execve(file, argv.size() ? argv : Argv {file.c_str()}, envp);
  } else if (pid < 0) [[unlikely]] {
    throw std::system_error {errno, std::system_category(), "failed to fork a new process"};
  }

  return pid;
}

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

template <typename FIRST, typename...REST>
requires (!std::is_same_v<Argv, std::decay_t<FIRST>>)
[[nodiscard("A caller is responsible for waiting for the child process")]]
pid_t execute(const int stdin,
              const int stdout,
              const int stderr,
              const path_t &file,
              FIRST&& first,
              REST&&...rest) {
  return execute(stdin, stdout, stderr,
                 file,
                 Argv {
                  file.c_str(),
                  stringify(std::forward<FIRST>(first)).c_str(),
                  (stringify(std::forward<REST>(rest)).c_str())...
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
      + stringify(d)
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
  if (pid <= 0)
    throw std::invalid_argument {"pid should be positive to specifiy exact one process: " + std::to_string(pid)};

  int status;
  if (::waitpid(pid, &status, 0) == -1) [[unlikely]]
    throw std::system_error{errno, std::system_category(), "Process(" + std::to_string(pid) + ") is not waitable"};

  if (!WIFEXITED(status)) [[unlikely]] {
    // FIXME: more granual error info should be offered
    throw std::runtime_error {
      "Process(" + std::to_string(pid) + ") is not terminated normally: " + std::to_string(status)
    };
  }

  return WEXITSTATUS(status);
}

inline int wait_for(const pid_t pid, const std::chrono::nanoseconds &d)
{
  if (pid <= 0) [[unlikely]]
    throw std::invalid_argument {"pid should be positive to specify exact one process: " + std::to_string(pid)};
  else if (d < d.zero()) [[unlikely]]
    throw std::invalid_argument {"duration must be positive for waiting process termination: " + std::to_string(pid)};


  sigset_t sigchld, sigold;
  if (sigemptyset(&sigchld) || sigaddset(&sigchld, SIGCHLD) || sigprocmask(SIG_BLOCK, &sigchld, &sigold)) [[unlikely]] {
    throw std::system_error {errno, std::system_category(), "failed to prepare to wait SIGCHLD"};
  }

  int wait_result {};
  siginfo_t info {};
  info.si_pid = 0;
  const auto begin = std::chrono::steady_clock::now();
  while (info.si_pid != pid && !wait_result) {
    const auto time_left {d - (std::chrono::steady_clock::now() - begin)};
    if (time_left <= d.zero()) {
      wait_result = -1;
    } else {
      const timespec timeout {
        .tv_sec = std::chrono::duration_cast<std::chrono::seconds>(time_left).count(),
        .tv_nsec = (time_left % std::chrono::seconds{1}).count()
      };
      wait_result = sigtimedwait(&sigchld, &info, &timeout);
    }
    if (wait_result == -1 && errno == EINTR) {
      wait_result = 0;
    }
  }

  const auto is_member {sigismember(&sigold, SIGCHLD)};
  if (!is_member) {
    sigprocmask(SIG_UNBLOCK, &sigchld, nullptr);
  } else if (is_member < 0) {
    throw std::system_error {errno, std::system_category(), "failed to check SIGCHLD signal"};
  }

  if (wait_result < 0) {
    throw timeout_error {pid, d};
  } else if (wait_result != SIGCHLD) {
    throw std::runtime_error {"unexpected signal (not SIGCHLD) is got: " + std::to_string(wait_result)};
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
  std::istringstream {line.substr(line.find_last_of(')')+1)} >> state >> ppid;
  return ppid;
}

} // namespace gh4ck3r::process
