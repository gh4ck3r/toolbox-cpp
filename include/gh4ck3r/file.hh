#pragma once
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace gh4ck3r::filesystem {
using namespace std::filesystem;
using path_t = std::filesystem::path;

using fd_t = int;

inline bool is_valid(const fd_t fd) {
  // fcntl returns -1 on error. If errno is EBADF, the fd is closed/invalid.
  if (fcntl(fd, F_GETFD) != -1) return true;

  if (errno == EBADF) return false;

  throw std::system_error {errno, std::system_category(),
    "failed to determine valid file descriptor"};
}

class unique_fd {
 public:
  unique_fd(fd_t fd) : fd_(fd) {}
  unique_fd(unique_fd&& uf) { *this = std::move(uf); }
  unique_fd& operator=(unique_fd&& rhs) {
    close(std::exchange(rhs.fd_, uninitialized));
    return *this;
  }
  ~unique_fd() noexcept { close(fd_); }

  unique_fd() = delete;
  unique_fd(const unique_fd&) = delete;
  unique_fd& operator=(const unique_fd&) = delete;

  inline operator int() const { return fd_; }
  inline operator bool() const { return is_valid(fd_); }

 private:
  static inline void close(fd_t fd) noexcept {
    int rv;
    do {
      rv = ::close(fd);
    } while (rv == -1 && errno == EINTR);
  }

 private:
  fd_t fd_;

  static inline constexpr fd_t uninitialized = -1;
};

template <typename R = std::vector<uint8_t>>
R load_file(const path_t &p) {
  if (const auto &stat = status(p); is_directory(stat)) [[unlikely]]
    throw std::invalid_argument {"directory can't be loaded: " + p.string()};
  else if (!is_regular_file(stat)) [[unlikely]]
    throw std::invalid_argument {"file not found: " + p.string()};

  std::ifstream is {p, std::ios::binary};
  is.unsetf(std::ios::skipws);
  return {std::istream_iterator<uint8_t>{is}, std::istream_iterator<uint8_t>{}};
}

inline size_t dir_siz(const path_t dir) {
  if (!is_directory(dir))
    [[unlikely]] throw std::invalid_argument {"dir_siz: no directory " + dir.string()};
  return std::distance(directory_iterator{dir}, directory_iterator{});
}

template <int mode>
inline bool access(const path_t &p) {
  return ::access(p.c_str(), mode) == 0;
}

inline bool is_readable(const path_t &path) {
  return access<F_OK | R_OK>(path);
};
inline bool is_writable(const path_t &path) {
  return access<F_OK | W_OK>(path);
}
inline bool is_executable(const path_t &path) {
  return access<F_OK | X_OK>(path);
}

[[nodiscard]]
inline fd_t create_anonymous_file() {
  auto fd = ::open(temp_directory_path().c_str(),
                 O_TMPFILE | O_RDWR | O_EXCL,
                 perms::owner_read | perms::owner_write);
  if (fd == -1) [[unlikely]] throw std::system_error {
    errno,
    std::system_category(),
    "create_anonymous_file failed"};

  return fd;
}

[[nodiscard]]
inline std::pair<fd_t, path_t> create_tempfile(path_t p = "XXXXXX")
{
  if (!p.has_parent_path()) p = temp_directory_path() / p;

  if (constexpr std::string_view suffix {"XXXXXX"};
      !p.filename().string().ends_with(suffix)) {
    if (is_directory(p)) p /= "";
    if (p.has_filename()) p += std::string{"."};
    p += suffix;
  }

  auto filepath { p.string() };
  const auto fd = ::mkostemp(filepath.data(), O_CLOEXEC);
  if (fd == -1) [[unlikely]] throw std::system_error {
    errno, std::system_category(), "failed to create tempfile"};

  return {fd, filepath};
}

class FileTrait
{
 public:
  virtual const path_t &path() const = 0;
  virtual fd_t fd() const = 0;
  virtual ~FileTrait() noexcept {}

  inline bool is_valid() const { return filesystem::is_valid(fd()); }
};

template <typename T>
concept FileT = std::is_base_of_v<FileTrait, T>;

template <FileT T>
class FileWriter
{
  std::unique_ptr<FileTrait> file_;

 public:
  template <typename...ARGS>
  explicit FileWriter(ARGS&&...args) :
    file_{std::make_unique<T>(std::forward<ARGS>(args)...)}
  {}
  ~FileWriter() noexcept {}

  inline fd_t fd() const { return file_->fd(); }
  inline path_t path() const { return file_->path(); }
  T& file() const { return reinterpret_cast<T&>(*file_); }

  bool write(const uint8_t *p, size_t l) {
    while (l) {
      const auto r = ::write(fd(), p, l);
      if (r < 0) {
        std::cerr << "failed to write to file: " << std::strerror(errno);
        return false;
      }
      l -= static_cast<size_t>(r);
    }
    return true;
  }

  template <typename C> requires requires (const C &c) {
    typename C::value_type;
    requires sizeof(typename C::value_type) == 1;

    c.data();
    c.size();
  }
  inline bool write(const C &v) {
    return write(reinterpret_cast<const uint8_t*>(v.data()), v.size());
  }

};

class AnonymousFile : public FileTrait
{
 public:
  AnonymousFile(): fd_ { create_anonymous_file() }
  {
    if (fd_) [[likely]] return;

    path_t p;
    std::tie(fd_, p) = create_tempfile();

    if (!remove(p)) [[unlikely]]
      throw std::runtime_error {"failed to unlink AnonymousFile"};
  }

  ~AnonymousFile() noexcept { ::close(fd_); }

 private:
  const path_t &path() const final { throw std::logic_error {"This is anonymous file"}; }

 protected:
  fd_t fd() const final { return fd_; }

 private:
  fd_t fd_;
};

class TempFile : public FileTrait
{
 public:
  explicit TempFile(const path_t k = "XXXXXX") {
    std::tie(fd_, path_) = create_tempfile(k);
  }
  ~TempFile() noexcept try {
    ::close(fd_);
    if (exists(path_)) remove(path_);
  } catch (const std::exception &e) {
    std::cerr << "exception while destruct TempFile: " << e.what();
  } catch (...) {
    std::cerr << "unknown exception while destruct TempFile";
  }

  const path_t &path() const final { return path_; }
  fd_t fd() const final { return fd_; }

 private:
  path_t path_;
  fd_t fd_ {-1};
};

class TempDir : public FileTrait {
 public:
  TempDir() = delete;
  explicit TempDir(const path_t &prefix) :
    path_(build_path(prefix))
  {}

  ~TempDir() noexcept try {
    remove_all(path_);
  } catch (const std::bad_alloc &e) {
    std::cerr << "failed to remove TempDir: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "Unknown exception while destruction TempDir" << std::endl;
  }

  inline const path_t &path() const override { return path_; }
  operator const path_t& () const { return path(); }

  path_t operator/(const path_t &rhs) const {
    return path() / rhs;
  }

 private:
  fd_t fd() const final { throw std::logic_error {"TempDir doesn't have a file descriptor"}; }

  path_t build_path(const path_t &prefix) const {
    const auto tmpl = (temp_directory_path() / prefix).concat(".XXXXXX").string();
    if (!::mkdtemp(const_cast<char*>(tmpl.data()))) [[unlikely]]
      throw std::system_error {errno, std::system_category(),
        "failed to create temporary directory"};
    return {tmpl.data()};
  }

  friend inline std::ostream &operator<<(std::ostream &os, const TempDir &tmpdir) {
    return os << tmpdir.path();
  }

 private:
  path_t path_;
};

template <typename Clock> requires requires {
  typename Clock::time_point;
  typename Clock::duration;
  Clock::now();
}
typename Clock::time_point clock_cast(const file_time_type &filetime) {
#if __cpp_lib_chrono >= 201907
  // XXX: Following will not cause the time drift but not tested yet.
  return std::chrono::clock_cast<Clock>(filetime);
#else
  // XXX: Consecutive calls to now() cause slight time drift.
  //      To ensure consistency, compute it once and reuse the value.
  static const auto G_CLOCK_OFFSET = [] {
    const auto f = file_time_type::clock::now();
    const auto c = Clock::now();
    return c.time_since_epoch() - f.time_since_epoch();
  }();
  return typename Clock::time_point {
    filetime.time_since_epoch() + G_CLOCK_OFFSET};
#endif
}

inline void copy_attrs(const path_t &from, const path_t &to, const directory_entry &src = {})
{
  if (src.is_directory())
    for (const auto &e : directory_iterator{src}) copy_attrs(from, to, e);

  const auto &dst = (!src.exists() || src == from) ?
    to : to / src.path().lexically_relative(from);
  last_write_time(dst, last_write_time(from));
}

inline bool copy_all(const path_t &from, const path_t &to)
{
  if (from == to) [[unlikely]] return false;

  std::error_code ec;
  copy(from, to, copy_options::recursive, ec);
  if (!ec) [[likely]] copy_attrs(from, to, directory_entry {from});

  if (ec) [[unlikely]]
    std::cerr << "failed to copy " << from << " to " << to
      << ": " << ec.message();

  return true;
}

} // namespace gh4ck3r::filesystem
