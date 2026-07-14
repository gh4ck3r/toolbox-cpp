#include "gh4ck3r/file.hh"
#include <filesystem>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <fcntl.h>

TEST(is_valid, valid)
{
  EXPECT_TRUE(gh4ck3r::file::is_valid(STDIN_FILENO));
  EXPECT_TRUE(gh4ck3r::file::is_valid(STDOUT_FILENO));
  EXPECT_TRUE(gh4ck3r::file::is_valid(STDERR_FILENO));
}

TEST(is_valid, invalid)
{
  EXPECT_FALSE(gh4ck3r::file::is_valid(-1));
}

TEST(is_valid, closed)
{
  const int fd = ::open(__FILE__, O_RDONLY);
  ASSERT_NE(-1, fd);
  EXPECT_TRUE(gh4ck3r::file::is_valid(fd));
  ::close(fd);
  EXPECT_FALSE(gh4ck3r::file::is_valid(fd));
}

class unique_fd_Test : public ::testing::Test {
 protected:
  using unique_fd = gh4ck3r::file::unique_fd;
};

TEST_F(unique_fd_Test, basic)
{
  const int fd = ::open(__FILE__, O_RDONLY);
  ASSERT_NE(-1, fd);

  EXPECT_TRUE(gh4ck3r::file::is_valid(fd));
  {
    unique_fd ufd { fd };
    EXPECT_EQ(fd, ufd);
    EXPECT_TRUE(ufd);
  }

  EXPECT_FALSE(gh4ck3r::file::is_valid(fd));
}

TEST(create_tempfile, default)
{
  const auto [fd, path] = gh4ck3r::file::create_tempfile();
  ASSERT_GE(fd, 0);
  EXPECT_EQ(std::filesystem::file_size(path), 0);
  EXPECT_TRUE(remove(path));

  EXPECT_EQ(path.parent_path(), std::filesystem::temp_directory_path());
  EXPECT_TRUE(path.has_filename());
  EXPECT_FALSE(path.has_extension());
}

TEST(create_tempfile, named)
{
  constexpr std::string_view filename {"hello"};
  const auto [fd, path] = gh4ck3r::file::create_tempfile(filename);
  EXPECT_TRUE(remove(path));

  EXPECT_EQ(path.parent_path(), std::filesystem::temp_directory_path());
  EXPECT_EQ(path.stem(), filename);
  EXPECT_TRUE(path.has_extension());
}

TEST(create_tempfile, designated_directory)
{
  const auto tempdir = std::filesystem::read_symlink("/proc/self/cwd") / "";
  ASSERT_FALSE(tempdir.has_filename());

  const auto [fd, path] = gh4ck3r::file::create_tempfile(tempdir);
  EXPECT_TRUE(remove(path));

  EXPECT_TRUE(equivalent(path.parent_path(), tempdir));
  EXPECT_TRUE(path.has_filename());
  EXPECT_FALSE(path.has_extension());
}

TEST(load_file, default)
{
  const auto [fd, path] = gh4ck3r::file::create_tempfile();

  constexpr std::string_view content {"0123456789"};
  std::ofstream {path} << content;

  EXPECT_EQ(gh4ck3r::file::load_file(path),
            std::vector<uint8_t>(content.begin(), content.end()));

  EXPECT_TRUE(remove(path));
}

TEST(load_file, string)
{
  const auto [fd, path] = gh4ck3r::file::create_tempfile();

  constexpr std::string_view content {"Hello World!"};
  std::ofstream {path} << content;

  EXPECT_EQ(gh4ck3r::file::load_file<std::string>(path), content);

  EXPECT_TRUE(remove(path));
}

TEST(load_file, invalid_argument)
{
  EXPECT_THROW(gh4ck3r::file::load_file("/proc"), std::invalid_argument);
  EXPECT_THROW(gh4ck3r::file::load_file("/dev/stdin"), std::invalid_argument);
}

TEST(dir_siz, default)
{
  const auto tempdir = std::filesystem::read_symlink("/proc/self/cwd") / "";
  ASSERT_FALSE(tempdir.has_filename());

  const auto before = gh4ck3r::file::dir_siz(tempdir);
  const auto [fd, path] = gh4ck3r::file::create_tempfile(tempdir);
  EXPECT_EQ(before + 1, gh4ck3r::file::dir_siz(tempdir));
  EXPECT_TRUE(remove(path));
  EXPECT_EQ(before, gh4ck3r::file::dir_siz(tempdir));
}

TEST(is_readable, default)
{
  EXPECT_TRUE(gh4ck3r::file::is_readable(
    std::filesystem::read_symlink("/proc/self/exe")));
}

TEST(is_writable, default)
{
  EXPECT_TRUE(gh4ck3r::file::is_readable(
    std::filesystem::read_symlink("/proc/self/exe")));
}

TEST(is_executable, default)
{
  EXPECT_TRUE(gh4ck3r::file::is_executable(
    std::filesystem::read_symlink("/proc/self/exe")));
}

TEST(FileWriter, default)
{
  struct FileMock : gh4ck3r::file::FileTrait {
    MOCK_METHOD(std::filesystem::path, path, (), (const override));
    MOCK_METHOD(gh4ck3r::file::fd_t, fd, (), (const override));
  };
  gh4ck3r::file::FileWriter<FileMock> writer {};
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
    gh4ck3r::file::AnonymousFile concrete;
    gh4ck3r::file::FileTrait &f {concrete};

    EXPECT_THROW(f.path(), std::logic_error);
    EXPECT_TRUE(f.is_valid());

    fd = f.fd();
  }
  EXPECT_FALSE(gh4ck3r::file::is_valid(fd));
}

TEST(TempFile, basic)
{
  int fd = -1;
  {
    gh4ck3r::file::TempFile concrete;
    gh4ck3r::file::FileTrait &f {concrete};

    EXPECT_TRUE(f.is_valid());
    EXPECT_GE(f.path().filename().string().size(), 6);

    fd = f.fd();
  }
  EXPECT_FALSE(gh4ck3r::file::is_valid(fd));
}

TEST(TempDir, basic)
{
  std::filesystem::path tempdir;
  {
    const std::filesystem::path name {"temp-prefix"};
    gh4ck3r::file::TempDir d{ name};
    EXPECT_TRUE(std::filesystem::is_directory(d));

    EXPECT_EQ(d.path().parent_path(), std::filesystem::temp_directory_path()) << d;
    EXPECT_EQ(d.path().stem(), name) << d;
    EXPECT_TRUE(d.path().has_extension()) << d;

    tempdir = d;
  }
  EXPECT_FALSE(std::filesystem::exists(tempdir));
}

TEST(TempDir, extension)
{
  const std::filesystem::path name {"temp-prefix.ext"};
  gh4ck3r::file::TempDir d{ name};
  EXPECT_TRUE(std::filesystem::is_directory(d));

  EXPECT_EQ(d.path().parent_path(), std::filesystem::temp_directory_path()) << d;
  EXPECT_EQ(d.path().stem(), name) << d;
  EXPECT_NE(d.path().extension(), name.extension()) << d;
}

TEST(file_time_type, system_clock)
{
  using std::chrono::system_clock;
  using namespace std::chrono_literals;
  using namespace std::string_view_literals;

  const auto before = system_clock::now();
  std::this_thread::sleep_for(2ms);  // XXX: suppresss the time drift

  gh4ck3r::file::FileWriter<gh4ck3r::file::TempFile> tmpfile;

  const auto after = system_clock::now();
  ASSERT_LE(before, after);

  const auto ftime =
    gh4ck3r::file::cast_to<system_clock>(last_write_time(tmpfile.path()));
  EXPECT_LE(before, ftime);
  EXPECT_GE(after, ftime);
}

TEST(file_time_type, steady_clock)
{
  using std::chrono::steady_clock;
  using namespace std::chrono_literals;
  using namespace std::string_view_literals;

  const auto before = steady_clock::now();
  std::this_thread::sleep_for(2ms);  // XXX: suppresss the time drift

  gh4ck3r::file::FileWriter<gh4ck3r::file::TempFile> tmpfile;

  const auto after = steady_clock::now();
  ASSERT_LE(before, after);

  const auto ftime =
    gh4ck3r::file::cast_to<steady_clock>(last_write_time(tmpfile.path()));
  EXPECT_LE(before, ftime);
  EXPECT_GE(after, ftime);
}
