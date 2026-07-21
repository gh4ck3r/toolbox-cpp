#include <gh4ck3r/process.hh>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>

using namespace gh4ck3r::process;

TEST(process_info, nameof)
{
  using gh4ck3r::process::nameof;
  EXPECT_EQ(nameof(getpid()), path_t{__FILE_NAME__}.stem());

  ASSERT_EQ(read_symlink(path_t{"/sbin/init"}).filename(), "systemd");
  EXPECT_EQ(nameof(1), "systemd");
}

TEST(process_info, cmdof)
{
  using gh4ck3r::process::cmdof;
  const auto cmd {cmdof(getpid())};
  EXPECT_FALSE(cmd.front().empty());

  const path_t path { cmd.front() };
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(path.filename(), std::filesystem::path{__FILE_NAME__}.stem());
}

TEST(process_info, execof)
{
  using gh4ck3r::process::execof;
  using gh4ck3r::process::is_executable;

  const auto executable { execof(getpid()) };
  EXPECT_TRUE(is_executable(executable));

  const auto filename = std::filesystem::path {__FILE_NAME__}.replace_extension();
  EXPECT_EQ(executable.filename(), filename);
}

TEST(process_info, exists)
{
  using gh4ck3r::process::exists;
  EXPECT_TRUE(exists(getpid()));
  EXPECT_TRUE(exists(getppid()));
  EXPECT_THROW(exists(0), std::invalid_argument);
}

TEST(process_exec, simple)
{
  using gh4ck3r::process::execute;
  using gh4ck3r::process::ppidof;
  using gh4ck3r::process::wait;

  const auto pid = execute("/bin/true");
  ASSERT_GE(pid, 0);
  EXPECT_EQ(ppidof(pid), getpid());

  EXPECT_EQ(wait(pid), 0);
}

TEST(process_exec, redirect_stdout)
{
  using gh4ck3r::process::execute;
  using gh4ck3r::process::ppidof;
  using gh4ck3r::process::wait;

  int pipefd[2];
  ASSERT_NE(pipe2(pipefd, O_CLOEXEC), -1);

  const auto pid = execute(STDIN_FILENO, pipefd[1], STDERR_FILENO,
                           "/bin/echo", "hello", "world");
  ASSERT_GE(pid, 0);
  EXPECT_EQ(ppidof(pid), getpid());

  EXPECT_EQ(wait(pid), 0);
  EXPECT_EQ(::close(pipefd[1]), 0);

  std::string buf {"hello, world"};
  buf.erase(read(pipefd[0], buf.data(), buf.size()));
  EXPECT_EQ(buf, "hello world\n");

  EXPECT_EQ(::close(pipefd[0]), 0);
}

TEST(process_exec, wait_timeout)
{
  using gh4ck3r::process::execute;
  using gh4ck3r::process::ppidof;
  using gh4ck3r::process::wait;
  using gh4ck3r::process::wait_for;
  using gh4ck3r::process::exit_code;

  const auto pid = execute("/bin/sleep", 2);
  ASSERT_GE(pid, 0);

  using namespace std::chrono_literals;
  EXPECT_THROW(wait_for(pid, 100ms), gh4ck3r::process::timeout_error);

  ::kill(pid, SIGKILL);
  EXPECT_EQ(wait(pid), static_cast<int>(exit_code::signaled) + SIGKILL ) << "should be killed by SIGKILL";
}

struct EnvTest : ::testing::Test {
 protected:
  static size_t count_env_var() {
    size_t cnt {};
    for (char **p = environ; *p; ++p) ++cnt;
    return cnt;
  }
};

TEST_F(EnvTest, size)
{
  Env env {};
  EXPECT_EQ(env.size(), count_env_var());

  env.clear();
  EXPECT_EQ(env.size(), 0);
}

TEST_F(EnvTest, iterate)
{
  Env env {};
  auto cnt = env.size();

  for ([[maybe_unused]] const auto &[k, v] : env) {
    cnt--;
  }

  EXPECT_EQ(cnt, 0);
}

TEST(ppidof, malformed_name)
{
  constexpr std::string_view malformed_name {" ) "};

  const auto pid = fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    prctl(PR_SET_NAME, malformed_name.data());
    exit(0);
  }
  while (nameof(pid) != malformed_name);

  EXPECT_EQ(getpid(), ppidof(pid));
  EXPECT_EQ(wait(pid), 0);
}
