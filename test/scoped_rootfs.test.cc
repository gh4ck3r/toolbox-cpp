#include "gh4ck3r/scoped_rootfs.hh"
#include "gh4ck3r/process.hh"
#include <filesystem>
#include <fstream>
#include <string>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <linux/capability.h>
#include <sys/syscall.h>

using namespace gh4ck3r::filesystem;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;

namespace {

bool has_required_caps() {
  __user_cap_header_struct hdr {
    .version = _LINUX_CAPABILITY_VERSION_3,
    .pid = 0,
  };
  __user_cap_data_struct data[2] {};

  if (syscall(SYS_capget, &hdr, data) != 0) return false;
  const uint32_t mask = (1u << CAP_SYS_CHROOT) | (1u << CAP_SYS_ADMIN);
  return (data[0].effective & mask) == mask;
}

std::string read_all_fd(int fd) {
  std::string out;
  char buf[256];
  while (true) {
    ssize_t r = ::read(fd, buf, sizeof(buf));
    if (r <= 0) break;
    out.append(buf, static_cast<size_t>(r));
  }
  return out;
}

} // namespace

TEST(ScopedRootFilesystem, existing_root_dir_throws)
{
  TempDir existing_dir {"existing-root"};
  EXPECT_THROW((void)RootFilesystemFactory{existing_dir.path()}.create(),
               std::invalid_argument);
}

TEST(ScopedRootFilesystem, bind_and_mock_lifecycle)
{
  if (!has_required_caps()) {
    GTEST_SKIP() << "CAP_SYS_CHROOT and CAP_SYS_ADMIN capabilities are required";
  }

  const path_t root_dir = temp_directory_path() / ("scoped_rootfs_test_" + std::to_string(::getpid()));
  remove_all(root_dir);
  ASSERT_FALSE(exists(root_dir));

  {
    RootFilesystemFactory factory {root_dir};
    factory.bind("/bin")
           .bind("/usr")
           .bind("/lib");
    if (exists("/lib64")) {
      factory.bind("/lib64");
    }

    ScopedRootFilesystem root_fs = factory
      .mock("/bin/ls") // StrictMock by default
      .mock<::testing::NiceMock>("/bin/df")
      .create();

    EXPECT_EQ(current_path(), "/");
    EXPECT_TRUE(exists("/bin/ls"));
    EXPECT_TRUE(exists("/bin/df"));
    EXPECT_FALSE(exists("/home"));

    // Bound directories must be read-only
    {
      int fd = ::open("/bin/test_ro_file", O_CREAT | O_WRONLY, 0644);
      EXPECT_EQ(fd, -1);
      EXPECT_EQ(errno, EROFS);
    }

    auto &ls_mock = root_fs.mock("/bin/ls");

    // 1. Intercept "/bin/ls -l" via GMock and write custom stdout + exit code
    EXPECT_CALL(ls_mock, execute(ElementsAre("/bin/ls", "-l"), _, _, _))
      .WillOnce([](const auto & /*argv*/, int /*in_fd*/, int out_fd, int /*err_fd*/) {
        constexpr std::string_view msg = "mocked ls -l output\n";
        EXPECT_EQ(static_cast<ssize_t>(msg.size()), ::write(out_fd, msg.data(), msg.size()));
        return 42;
      });

    {
      int pfd[2];
      ASSERT_EQ(0, ::pipe2(pfd, O_CLOEXEC));
      const pid_t pid = gh4ck3r::process::execute(STDIN_FILENO, pfd[1], STDERR_FILENO,
                                                  "/bin/ls", "-l");
      ::close(pfd[1]);
      const std::string output = read_all_fd(pfd[0]);
      ::close(pfd[0]);
      EXPECT_EQ(42, gh4ck3r::process::wait(pid));
      EXPECT_EQ("mocked ls -l output\n", output);
    }

    // 2. Execute original "/bin/ls" when invoked with "/" (via explicit execute_original)
    EXPECT_CALL(ls_mock, execute(ElementsAre("/bin/ls", "/"), _, _, _))
      .WillOnce(Invoke(&ls_mock, &ExecutableMock::execute_original));

    {
      int pfd[2];
      ASSERT_EQ(0, ::pipe2(pfd, O_CLOEXEC));
      const pid_t pid = gh4ck3r::process::execute(STDIN_FILENO, pfd[1], STDERR_FILENO,
                                                  "/bin/ls", "/");
      ::close(pfd[1]);
      const std::string output = read_all_fd(pfd[0]);
      ::close(pfd[0]);
      EXPECT_EQ(0, gh4ck3r::process::wait(pid));
      EXPECT_NE(output.find("bin"), std::string::npos) << output;
    }

    // 3. Unspecified EXPECT_CALL on NiceMock ("/bin/df") executes original binary without warning
    {
      int pfd[2];
      ASSERT_EQ(0, ::pipe2(pfd, O_CLOEXEC));
      const pid_t pid = gh4ck3r::process::execute(STDIN_FILENO, pfd[1], STDERR_FILENO,
                                                  "/bin/df", "--version");
      ::close(pfd[1]);
      const std::string output = read_all_fd(pfd[0]);
      ::close(pfd[0]);
      EXPECT_EQ(0, gh4ck3r::process::wait(pid));
      EXPECT_FALSE(output.empty());
    }
  }

  // Once ScopedRootFilesystem is destroyed, root_dir must be cleaned up
  EXPECT_FALSE(exists(root_dir));
}
