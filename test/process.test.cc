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
  EXPECT_EQ(nameof(getpid()), path_t{__FILE_NAME__}.stem());

  EXPECT_FALSE(nameof(1).empty());
  std::error_code ec;
  if (std::filesystem::is_symlink("/sbin/init", ec)) {
    const auto init_name = read_symlink(path_t{"/sbin/init"}).filename();
    EXPECT_EQ(nameof(1), init_name);
  }
}

TEST(process_info, cmdof)
{
  const auto cmd {cmdof(getpid())};
  EXPECT_FALSE(cmd.front().empty());

  const path_t path { cmd.front() };
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(path.filename(), std::filesystem::path{__FILE_NAME__}.stem());
}

TEST(process_info, execof)
{
  const auto executable { execof(getpid()) };
  EXPECT_TRUE(is_executable(executable));

  const auto filename = std::filesystem::path {__FILE_NAME__}.replace_extension();
  EXPECT_EQ(executable.filename(), filename);
}

TEST(process_info, exists)
{
  EXPECT_TRUE(exists(getpid()));
  EXPECT_TRUE(exists(getppid()));
  EXPECT_THROW(exists(0), std::invalid_argument);
}

TEST(process_exec, simple)
{
  const auto pid = execute("/bin/true");
  ASSERT_GE(pid, 0);
  EXPECT_EQ(ppidof(pid), getpid());

  EXPECT_EQ(wait(pid), 0);
}

TEST(process_exec, redirect_stdout)
{
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
  const auto pid = execute("/bin/sleep", 2);
  ASSERT_GE(pid, 0);

  using namespace std::chrono_literals;
  EXPECT_THROW(wait_for(pid, 500us), timeout_error);
  EXPECT_THROW(wait_for(pid, 100ms), timeout_error);

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

TEST_F(EnvTest, copy_and_move)
{
  Env env1 {};
  Env env2 = env1;
  EXPECT_EQ(env2.size(), env1.size());

  Env env3 = std::move(env2);
  EXPECT_EQ(env3.size(), env1.size());
}

TEST(ppidof, malformed_name)
{
  constexpr std::string_view malformed_name {" ) "};

  int pipefd[2];
  ASSERT_NE(pipe(pipefd), -1);

  const auto pid = fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    prctl(PR_SET_NAME, malformed_name.data());
    ::close(pipefd[1]);
    ::pause();
  }

  ::close(pipefd[1]);
  char dummy;
  [[maybe_unused]] auto r = ::read(pipefd[0], &dummy, 1);
  ::close(pipefd[0]);

  EXPECT_EQ(nameof(pid), malformed_name);
  EXPECT_EQ(getpid(), ppidof(pid));

  ::kill(pid, SIGTERM);
  EXPECT_EQ(wait(pid), static_cast<int>(exit_code::signaled) + SIGTERM);
}

TEST(process_exec, non_executable)
{
  EXPECT_THROW((void)execute("/non_existent_binary_file_12345"), std::invalid_argument);
}

static void sigusr1_handler(int) {}

TEST(process_exec, wait_for_eintr)
{
  struct sigaction sa {};
  sa.sa_handler = sigusr1_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; // Notice: SA_RESTART is NOT set!
  sigaction(SIGUSR1, &sa, nullptr);

  const auto pid = execute("/bin/sleep", "2");
  ASSERT_GE(pid, 0);

  // Send SIGUSR1 to ourselves after 50ms using a thread
  std::thread signal_thread([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ::kill(::getpid(), SIGUSR1);
  });

  using namespace std::chrono_literals;
  // If wait_for does NOT retry on EINTR, it throws std::system_error (Interrupted system call)
  EXPECT_NO_THROW({
    const auto ec = wait_for(pid, 3s);
    EXPECT_EQ(ec, 0);
  });

  signal_thread.join();
}
