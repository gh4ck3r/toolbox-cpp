#include "gh4ck3r/file.hh"
#include <filesystem>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <fcntl.h>

using namespace gh4ck3r::filesystem;

TEST(is_valid, valid)
{
  EXPECT_TRUE(is_valid(STDIN_FILENO));
  EXPECT_TRUE(is_valid(STDOUT_FILENO));
  EXPECT_TRUE(is_valid(STDERR_FILENO));
}

TEST(is_valid, invalid)
{
  EXPECT_FALSE(is_valid(-1));
}

TEST(is_valid, closed)
{
  const int fd = ::open(__FILE__, O_RDONLY);
  ASSERT_NE(-1, fd);
  EXPECT_TRUE(is_valid(fd));
  ::close(fd);
  EXPECT_FALSE(is_valid(fd));
}

TEST(unique_fd, basic)
{
  const int fd = ::open(__FILE__, O_RDONLY);
  ASSERT_NE(-1, fd);

  EXPECT_TRUE(is_valid(fd));
  {
    unique_fd ufd { fd };
    EXPECT_EQ(fd, ufd);
    EXPECT_TRUE(ufd);
  }

  EXPECT_FALSE(is_valid(fd));
}

TEST(create_tempfile, default)
{
  const auto [fd, path] = create_tempfile();
  ASSERT_GE(fd, 0);
  EXPECT_EQ(file_size(path), 0);
  EXPECT_TRUE(remove(path));

  EXPECT_EQ(path.parent_path(), temp_directory_path());
  EXPECT_TRUE(path.has_filename());
  EXPECT_FALSE(path.has_extension());
}

TEST(create_tempfile, named)
{
  constexpr std::string_view filename {"hello"};
  const auto [fd, path] = create_tempfile(filename);
  EXPECT_TRUE(remove(path));

  EXPECT_EQ(path.parent_path(), temp_directory_path());
  EXPECT_EQ(path.stem(), filename);
  EXPECT_TRUE(path.has_extension());
}

TEST(create_tempfile, designated_directory)
{
  const auto tempdir = read_symlink("/proc/self/cwd") / "";
  ASSERT_FALSE(tempdir.has_filename());

  const auto [fd, path] = create_tempfile(tempdir);
  EXPECT_TRUE(remove(path));

  EXPECT_TRUE(equivalent(path.parent_path(), tempdir));
  EXPECT_TRUE(path.has_filename());
  EXPECT_FALSE(path.has_extension());
}

TEST(load_file, default)
{
  const auto [fd, path] = create_tempfile();

  constexpr std::string_view content {"0123456789"};
  std::ofstream {path} << content;

  EXPECT_EQ(load_file(path),
            std::vector<uint8_t>(content.begin(), content.end()));

  EXPECT_TRUE(remove(path));
}

TEST(load_file, string)
{
  const auto [fd, path] = create_tempfile();

  constexpr std::string_view content {"Hello World!"};
  std::ofstream {path} << content;

  EXPECT_EQ(load_file<std::string>(path), content);

  EXPECT_TRUE(remove(path));
}

TEST(load_file, invalid_argument)
{
  EXPECT_THROW(load_file("/proc"), std::invalid_argument);
  EXPECT_THROW(load_file("/dev/stdin"), std::invalid_argument);
}

TEST(dir_siz, default)
{
  const auto tempdir = read_symlink("/proc/self/cwd") / "";
  ASSERT_FALSE(tempdir.has_filename());

  const auto before = dir_siz(tempdir);
  const auto [fd, path] = create_tempfile(tempdir);
  EXPECT_EQ(before + 1, dir_siz(tempdir));
  EXPECT_TRUE(remove(path));
  EXPECT_EQ(before, dir_siz(tempdir));
}

TEST(is_readable, default)
{
  EXPECT_TRUE(is_readable(read_symlink(path{"/proc/self/exe"})));
}

TEST(is_writable, default)
{
  EXPECT_TRUE(is_writable(read_symlink(path{"/proc/self/exe"})));
}

TEST(is_executable, default)
{
  EXPECT_TRUE(is_executable(read_symlink(path{"/proc/self/exe"})));
}

TEST(FileWriter, default)
{
  struct FileMock : FileTrait {
    MOCK_METHOD(const path_t&, path, (), (const override));
    MOCK_METHOD(fd_t, fd, (), (const override));
  };
  FileWriter<FileMock> writer {};
  auto & filemock = writer.file();

  EXPECT_CALL(filemock, fd()).WillOnce(::testing::Return(10));
  EXPECT_EQ(10, writer.fd());

  EXPECT_CALL(filemock, fd()).WillOnce(::testing::Return(20));
  EXPECT_EQ(20, writer.fd());

  int pipefd[2];
  ASSERT_EQ(0, pipe2(pipefd, O_CLOEXEC));

  EXPECT_CALL(filemock, fd()).WillOnce(::testing::Return(pipefd[1]));
  const std::string s1{"hello"};
  writer.write(std::string{s1});
  ASSERT_EQ(0, ::close(pipefd[1]));

  std::string s2(s1.size(), 0x00);
  ASSERT_EQ(s2.size(), ::read(pipefd[0], s2.data(), s2.size()));
  ASSERT_EQ(0, ::close(pipefd[0]));

  EXPECT_EQ(s1, s2);
}

TEST(AnonymousFile, basic)
{
  int fd = -1;
  {
    AnonymousFile concrete;
    FileTrait &f {concrete};

    EXPECT_THROW(f.path(), std::logic_error);
    EXPECT_TRUE(f.is_valid());

    fd = f.fd();
  }
  EXPECT_FALSE(is_valid(fd));
}

TEST(TempFile, basic)
{
  int fd = -1;
  {
    TempFile concrete;
    FileTrait &f {concrete};

    EXPECT_TRUE(f.is_valid());
    EXPECT_GE(f.path().filename().string().size(), 6);

    fd = f.fd();
  }
  EXPECT_FALSE(is_valid(fd));
}

TEST(TempDir, basic)
{
  path_t tempdir;
  {
    const path_t name {"temp-prefix"};
    gh4ck3r::filesystem::TempDir d {name};
    EXPECT_TRUE(is_directory(d));

    EXPECT_EQ(d.path().parent_path(), temp_directory_path()) << d;
    EXPECT_EQ(d.path().stem(), name) << d;
    EXPECT_TRUE(d.path().has_extension()) << d;

    tempdir = d;
  }
  EXPECT_FALSE(exists(tempdir));
}

TEST(TempDir, extension)
{
  const path_t name {"temp-prefix.ext"};
  gh4ck3r::filesystem::TempDir d{ name};
  EXPECT_TRUE(is_directory(d));

  EXPECT_EQ(d.path().parent_path(), temp_directory_path()) << d;
  EXPECT_EQ(d.path().stem(), name) << d;
  EXPECT_NE(d.path().extension(), name.extension()) << d;
}

TEST(file_time_type, system_clock)
{
  using std::chrono::system_clock;
  using namespace std::chrono_literals;

  const auto before = system_clock::now();
  std::this_thread::sleep_for(2ms);  // XXX: suppresss the time drift

  FileWriter<TempFile> tmpfile;

  const auto after = system_clock::now();
  ASSERT_LE(before, after);

  const auto ftime = clock_cast<system_clock>(last_write_time(tmpfile.path()));
  EXPECT_LE(before, ftime);
  EXPECT_GE(after, ftime);
}

TEST(file_time_type, steady_clock)
{
  using std::chrono::steady_clock;
  using namespace std::chrono_literals;

  const auto before = steady_clock::now();
  std::this_thread::sleep_for(2ms);  // XXX: suppresss the time drift

  FileWriter<TempFile> tmpfile;

  const auto after = steady_clock::now();
  ASSERT_LE(before, after);

  const auto ftime = clock_cast<steady_clock>(last_write_time(tmpfile.path()));
  EXPECT_LE(before, ftime);
  EXPECT_GE(after, ftime);
}

TEST(copy_attrs, file)
{
  const gh4ck3r::filesystem::TempFile src;
  ASSERT_TRUE(is_regular_file(src.path()));

  using namespace std::chrono_literals;
  std::this_thread::sleep_for(5ms);

  const auto dst{path{src.path()}.replace_extension(".backup")};
  ASSERT_FALSE(is_regular_file(dst));
  ASSERT_TRUE(copy_file(src.path(), dst));
  ASSERT_TRUE(exists(dst));
  EXPECT_NE(last_write_time(src.path()), last_write_time(dst));

  gh4ck3r::filesystem::copy_attrs(src.path(), dst);
  EXPECT_EQ(last_write_time(src.path()), last_write_time(dst));

  EXPECT_TRUE(remove(dst)) << dst;
}

TEST(copy_attrs, dir)
{
  const gh4ck3r::filesystem::TempDir src{"test"};
  ASSERT_TRUE(is_directory(src.path()));

  using namespace std::chrono_literals;
  std::this_thread::sleep_for(5ms);

  const auto dst{path{src.path()}.replace_extension(".backup")};
  ASSERT_FALSE(exists(dst));
  copy(src.path(), dst);
  ASSERT_TRUE(exists(dst));
  EXPECT_NE(last_write_time(src.path()), last_write_time(dst));

  gh4ck3r::filesystem::copy_attrs(src.path(), dst);
  EXPECT_EQ(last_write_time(src.path()), last_write_time(dst));

  EXPECT_TRUE(remove(dst)) << dst;
}


TEST(copy_all, dir)
{
  const TempDir srcdir {"test-src"};
  ASSERT_TRUE(exists(srcdir.path()));
  const TempDir dstdir {"test-dst"};
  ASSERT_TRUE(exists(dstdir.path()));

  TempFile f1 {srcdir };
  ASSERT_TRUE(is_regular_file(f1.path()));
  ASSERT_EQ(f1.path().parent_path(), srcdir) << f1.path();

  TempFile f2 { srcdir };
  ASSERT_TRUE(is_regular_file(f2.path()));
  ASSERT_EQ(f2.path().parent_path(), srcdir);

  const auto subdir {path{"1st"}/"2nd"/"3rd"};
  ASSERT_TRUE(create_directories(srcdir / subdir));
  TempFile f3 { srcdir / subdir };
  ASSERT_EQ(f3.path().parent_path(), srcdir / subdir);

  EXPECT_TRUE(copy_all(srcdir, dstdir));

  EXPECT_TRUE(is_directory(dstdir / subdir));
  EXPECT_TRUE(is_regular_file(dstdir / f1.path().filename()));
  EXPECT_TRUE(is_regular_file(dstdir / f2.path().filename()));
  EXPECT_TRUE(is_regular_file(dstdir / subdir / f3.path().filename()));

  constexpr auto EXPECT_TS_EQ = [] (const auto &lhs, const auto &rhs) {
    const auto diff = duration_cast<std::chrono::milliseconds>(
        last_write_time(lhs) - last_write_time(rhs));
    EXPECT_LE(abs(diff.count()), 2); // XXX:margin for time drift
  };
  EXPECT_TS_EQ(srcdir.path(), dstdir.path());
  EXPECT_TS_EQ(srcdir.path() / subdir, dstdir.path() / subdir);

  EXPECT_TS_EQ(f1.path(), dstdir / f1.path().filename());
  EXPECT_TS_EQ(f2.path(), dstdir / f2.path().filename());
  EXPECT_TS_EQ(f3.path(), dstdir / subdir / f3.path().filename());
}
