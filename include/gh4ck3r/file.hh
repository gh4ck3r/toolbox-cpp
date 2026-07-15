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

namespace gh4ck3r::file {

using fd_t = int;

namespace fs = std::filesystem;

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
  unique_fd(unique_fd&& uf) : fd_(std::exchange(uf.fd_, uninitialied)) {}
  ~unique_fd() noexcept { if (fd_ != uninitialied) { ::close(fd_); } }

  unique_fd() = delete;
  unique_fd(unique_fd&) = delete;
  unique_fd(const unique_fd&) = delete;
  unique_fd& operator=(const unique_fd&) = delete;

  inline operator int() const { return fd_; }
  inline operator bool() const { return fd_ != uninitialied && is_valid(fd_); }

 private:
  fd_t fd_;

  static inline constexpr fd_t uninitialied = -1;
};

template <typename R = std::vector<uint8_t>>
R load_file(const fs::path &p) {
  if (const auto &stat = status(p); is_directory(stat))
    [[unlikely]] throw std::invalid_argument {"directory can't be loaded: " + p.string()};
  else if (!fs::is_regular_file(stat))
    [[unlikely]] throw std::invalid_argument {"file not found: " + p.string()};

  std::ifstream is {p, std::ios::binary};
  is.unsetf(std::ios::skipws);
  return {std::istream_iterator<uint8_t>{is}, std::istream_iterator<uint8_t>{}};
}

template <typename T = fs::path>
size_t dir_siz(const T dir) {
  using fs::directory_iterator;
  return std::distance(directory_iterator{dir}, directory_iterator{});
}

template <int mode>
inline bool access(const fs::path &p) {
  return ::access(p.c_str(), mode) == 0;
}

inline bool is_readable(const fs::path &path) {
  return access<F_OK | R_OK>(path);
};
inline bool is_writable(const fs::path &path) {
  return access<F_OK | W_OK>(path);
}
inline bool is_executable(const fs::path &path) {
  return access<F_OK | X_OK>(path);
}

[[nodiscard]]
inline fd_t create_anonymous_file() {
  auto fd = ::open(fs::temp_directory_path().c_str(),
                 O_TMPFILE | O_RDWR | O_EXCL,
                 fs::perms::owner_read | fs::perms::owner_write);
  if (fd == -1) [[unlikely]] throw std::system_error {
    errno,
    std::system_category(),
    "create_anonymous_file failed"};

  return fd;
}

[[nodiscard]]
inline std::pair<fd_t, fs::path> create_tempfile(fs::path p = "XXXXXX")
{
  if (!p.has_parent_path()) p = fs::temp_directory_path() / p;

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
  virtual const fs::path &path() const = 0;
  virtual fd_t fd() const = 0;
  virtual ~FileTrait() noexcept {}

  inline bool is_valid() const {
    return file::is_valid(fd());
  }
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
  inline fs::path path() const { return file_->path(); }
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
  using perms = fs::perms;

 public:
  AnonymousFile(): fd_ { create_anonymous_file() }
  {
    if (fd_) [[likely]] return;

    fs::path p;
    std::tie(fd_, p) = create_tempfile();

    if (!fs::remove(p)) [[unlikely]]
      throw std::runtime_error {"failed to unlink AnonymousFile"};
  }

  ~AnonymousFile() noexcept { ::close(fd_); }

 private:
  const fs::path &path() const final { throw std::logic_error {"This is anonymous file"}; }

 protected:
  fd_t fd() const final { return fd_; }

 private:
  fd_t fd_;
};

class TempFile : public FileTrait
{
 public:
  explicit TempFile(const fs::path k = "XXXXXX") {
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

  const fs::path &path() const final { return path_; }
  fd_t fd() const final { return fd_; }

 private:
  fs::path path_;
  fd_t fd_ {-1};
};

class TempDir : public FileTrait {
 public:
  TempDir() = delete;
  explicit TempDir(const fs::path &prefix) :
    path_(build_path(prefix))
  {}

  ~TempDir() noexcept try {
    remove_all(path_);
  } catch (const std::bad_alloc &e) {
    std::cerr << "failed to remove TempDir: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "Unknown exception while destruction TempDir" << std::endl;
  }

  inline const fs::path &path() const override { return path_; }
  operator const fs::path& () const { return path(); }

  fs::path operator/(const fs::path &rhs) const {
    return path() / rhs;
  }

 private:
  fd_t fd() const final { throw std::logic_error {"TempDir doesn't have a file descriptor"}; }

  fs::path build_path(const fs::path &prefix) const {
    const auto tmpl = (fs::temp_directory_path() / prefix).concat(".XXXXXX").string();
    if (!::mkdtemp(const_cast<char*>(tmpl.data()))) [[unlikely]]
      throw std::system_error {errno, std::system_category(),
        "failed to create temporary directory"};
    return {tmpl.data()};
  }

  friend inline std::ostream &operator<<(std::ostream &os, const TempDir &tmpdir) {
    return os << tmpdir.path();
  }

 private:
  fs::path path_;
};


template <typename Clock> requires requires {
  typename Clock::time_point;
  typename Clock::duration;
  Clock::now();
}
typename Clock::time_point cast_to(const fs::file_time_type &filetime) {
  //return std::chrono::clock_cast<std::chrono::system_clock>(filetime);
  static const auto G_CLOCK_OFFSET = [] {
    // XXX: consequent calling of now() makes a little bit of time drift
    const auto f = fs::file_time_type::clock::now();
    const auto c = Clock::now();
    return c.time_since_epoch() - f.time_since_epoch();
  }();
  return typename Clock::time_point {
    filetime.time_since_epoch() + G_CLOCK_OFFSET};
}

inline void copy_attrs(const fs::path &from, const fs::path &to, const fs::directory_entry &src = {})
{
  if (src.is_directory())
    for (const auto &e : fs::directory_iterator{src}) copy_attrs(from, to, e);

  const auto &dst = (!src.exists() || src == from) ?
    to : to / src.path().lexically_relative(from);
  last_write_time(dst, last_write_time(from));
}

inline bool copy_all(const fs::path &from, const fs::path &to)
{
  if (from == to) [[unlikely]] return false;

  std::error_code ec;
  copy(from, to, fs::copy_options::recursive, ec);
  if (!ec) [[likely]] copy_attrs(from, to, fs::directory_entry {from});

  if (ec) [[unlikely]]
    std::cerr << "failed to copy " << from << " to " << to
      << ": " << ec.message();

  return true;
}

} // namespace gh4ck3r::file
