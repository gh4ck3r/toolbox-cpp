#include "gh4ck3r/scoped_rootfs.hh"
#include "gh4ck3r/file.hh"
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <optional>
#include <string>
#include <gtest/gtest-spi.h>
#include <linux/capability.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>

namespace {

namespace fs = std::filesystem;
using gh4ck3r::filesystem::scoped_rootfs::mock::Executable;
using gh4ck3r::filesystem::scoped_rootfs::RootFilesystemFactory;
using gh4ck3r::filesystem::TempDir;
using gh4ck3r::filesystem::unique_fd;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;

template <typename Base>
class FailingMock : public Base {
 public:
  FailingMock(fs::path command, fs::path original) :
    Base {std::move(command), std::move(original)}
  {
    throw std::runtime_error {"injected mock construction failure"};
  }
};

bool has_required_caps() {
  __user_cap_header_struct header {};
  header.version = _LINUX_CAPABILITY_VERSION_3;
  __user_cap_data_struct data[2] {};
  if (::syscall(SYS_capget, &header, data) != 0) return false;
  const uint32_t mask = (1u << CAP_SYS_CHROOT) | (1u << CAP_SYS_ADMIN);
  return (data[0].effective & mask) == mask;
}

void write_file(const fs::path &path, const std::string &contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output {path, std::ios::binary};
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output << contents;
}

std::string contents(const fs::path &path) {
  return gh4ck3r::filesystem::load_file<std::string>(path);
}

void write_fd(int fd, const std::string &text) {
  const char *data = text.data();
  std::size_t remaining = text.size();
  while (remaining) {
    const auto count = ::write(fd, data, remaining);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) throw std::system_error {errno, std::system_category(), "write mock output"};
    data += count;
    remaining -= static_cast<std::size_t>(count);
  }
}

void bind_libraries(RootFilesystemFactory &factory) {
  for (const auto *path : {"/lib", "/lib64", "/usr/lib", "/usr/lib64"})
    if (fs::is_directory(path)) factory.bind(path);
}

struct CommandResult {
  int code;
  std::string output;
  std::string error;
};

CommandResult run_command(const std::string &program, std::vector<std::string> args = {},
                          const std::string &input = {}, const std::string &argv0 = {}) {
  static std::atomic<unsigned int> sequence {0};
  const auto base = "/capture-" + std::to_string(sequence.fetch_add(1));
  write_file(base + ".in", input);
  unique_fd in {::open((base + ".in").c_str(), O_RDONLY | O_CLOEXEC)};
  unique_fd out {::open((base + ".out").c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
  unique_fd err {::open((base + ".err").c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
  args.insert(args.begin(), argv0.empty() ? program : argv0);
  std::vector<char *> argv;
  for (auto &arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  const auto pid = ::fork();
  if (pid < 0) throw std::system_error {errno, std::system_category(), "fork test command"};
  if (pid == 0) {
    if (::dup2(in, STDIN_FILENO) < 0 || ::dup2(out, STDOUT_FILENO) < 0 ||
        ::dup2(err, STDERR_FILENO) < 0) ::_exit(126);
    ::execv(program.c_str(), argv.data());
    ::_exit(127);
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {15};
  int status = 0;
  for (;;) {
    const auto waited = ::waitpid(pid, &status, WNOHANG);
    if (waited == pid) break;
    if (waited < 0 && errno == EINTR) continue;
    if (waited < 0) throw std::system_error {errno, std::system_category(), "wait test command"};
    if (std::chrono::steady_clock::now() >= deadline) {
      ::kill(pid, SIGKILL);
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
      throw std::runtime_error {"test command timed out: " + program};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds {5});
  }
  return {WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status),
          contents(base + ".out"), contents(base + ".err")};
}

TEST(ScopedRootFilesystemValidation, RejectsInvalidAndDuplicatePaths) {
  RootFilesystemFactory factory {"/unused-root"};
  EXPECT_THROW(factory.bind("/source", "/"), std::invalid_argument);
  EXPECT_THROW(factory.bind("/source", "/."), std::invalid_argument);
  EXPECT_THROW(factory.bind("/source", "/a/../escape"), std::invalid_argument);
  EXPECT_THROW(factory.bind("/source", "/.mock_ipc/sock"), std::invalid_argument);
  EXPECT_THROW(factory.mock("/.mock_orig/command"), std::invalid_argument);
  EXPECT_THROW(factory.mock(std::string {"/bad\0path", 9}), std::invalid_argument);
  factory.bind("/source", "/destination");
  EXPECT_THROW(factory.bind("/other", "/destination"), std::invalid_argument);
  factory.mock("/bin/agent");
  EXPECT_THROW(factory.mock("/bin/agent"), std::invalid_argument);
}

TEST(ScopedRootFilesystemValidation, ExistingRootIsNotOwnedOrModified) {
  TempDir directory {"rootfs-existing"};
  write_file(directory.path() / "keep", "existing data");
  EXPECT_THROW((void)RootFilesystemFactory {directory.path()}.create(), std::invalid_argument);
  EXPECT_EQ(contents(directory.path() / "keep"), "existing data");
}

TEST(ScopedRootFilesystemValidation, ContextReservesThreadUntilDestruction) {
  using gh4ck3r::filesystem::scoped_rootfs::detail::FilesystemContext;
  TempDir directory {"rootfs-reservation"};
  const auto root = directory.path() / "root";
  {
    FilesystemContext context {root};
    EXPECT_THROW((void)FilesystemContext {root}, std::logic_error);
    context.restore();
    EXPECT_THROW((void)FilesystemContext {root}, std::logic_error);
  }
  EXPECT_NO_THROW((void)FilesystemContext {root});
}

TEST(ScopedRootFilesystemValidation, FailedContextConstructionReleasesThreadReservation) {
  using gh4ck3r::filesystem::scoped_rootfs::detail::FilesystemContext;
  TempDir directory {"rootfs-reservation-failure"};
  EXPECT_THROW((void)FilesystemContext {directory.path()}, std::invalid_argument);
  EXPECT_NO_THROW((void)FilesystemContext {directory.path() / "root"});
}

class ScopedRootFilesystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!has_required_caps())
      GTEST_SKIP() << "CAP_SYS_CHROOT and CAP_SYS_ADMIN are required";
    directory_.emplace("scoped-rootfs-test");
    cwd_ = fs::current_path();
    ASSERT_EQ(::stat("/proc/thread-self/ns/mnt", &namespace_), 0);
  }

  void TearDown() override {
    if (!directory_) return;
    EXPECT_EQ(fs::current_path(), cwd_);
    struct stat current {};
    ASSERT_EQ(::stat("/proc/thread-self/ns/mnt", &current), 0);
    EXPECT_EQ(current.st_dev, namespace_.st_dev);
    EXPECT_EQ(current.st_ino, namespace_.st_ino);
    EXPECT_FALSE(fs::exists(root()));
  }

  fs::path root() const { return directory_->path() / "root"; }
  fs::path source() const { return directory_->path() / "source"; }
  std::optional<TempDir> directory_;
  fs::path cwd_;
  struct stat namespace_ {};
};

TEST_F(ScopedRootFilesystemTest, FreshRootRestoresRootWorkingDirectoryAndNamespace) {
  const auto outside = source() / "outside";
  write_file(outside, "outside");
  {
    auto scope = RootFilesystemFactory {root()}.create();
    EXPECT_EQ(fs::current_path(), "/");
    EXPECT_FALSE(fs::exists(outside));
    write_file("/inside/marker", "temporary");
    fs::current_path("/inside");
    EXPECT_EQ(contents("marker"), "temporary");
  }
  EXPECT_EQ(contents(outside), "outside");
}

TEST_F(ScopedRootFilesystemTest, RestoresDuringExceptionUnwinding) {
  EXPECT_THROW({
    auto scope = RootFilesystemFactory {root()}.create();
    write_file("/marker", "temporary");
    throw std::runtime_error {"leave root"};
  }, std::runtime_error);
}

TEST_F(ScopedRootFilesystemTest, OwnsARelativeRootPath) {
  const auto relative = root().lexically_relative(fs::current_path());
  ASSERT_TRUE(relative.is_relative());
  {
    auto scope = RootFilesystemFactory {relative}.create();
    write_file("/marker", "relative root");
    EXPECT_EQ(contents("/marker"), "relative root");
  }
}

TEST_F(ScopedRootFilesystemTest, CreatesMissingParentsButRemovesOnlyTheRequestedRoot) {
  const auto parent = directory_->path() / "missing-parent";
  const auto path = parent / "root";
  {
    auto scope = RootFilesystemFactory {path}.create();
    write_file("/marker", "nested path");
  }
  EXPECT_FALSE(fs::exists(path));
  EXPECT_TRUE(fs::is_directory(parent));
}

TEST_F(ScopedRootFilesystemTest, BindsDirectoriesAndFilesReadOnlyAtRequestedPaths) {
  write_file(source() / "file", "original");
  {
    auto scope = RootFilesystemFactory {root()}
      .bind(source(), "/readonly")
      .bind(source() / "file", "/individual").create();
    EXPECT_EQ(contents("/readonly/file"), "original");
    EXPECT_EQ(contents("/individual"), "original");
    int fd = ::open("/readonly/file", O_WRONLY | O_TRUNC);
    const int file_error = errno;
    if (fd >= 0) ::close(fd);
    EXPECT_LT(fd, 0);
    EXPECT_EQ(file_error, EROFS);
    fd = ::open("/readonly/new", O_WRONLY | O_CREAT, 0600);
    const int create_error = errno;
    if (fd >= 0) ::close(fd);
    EXPECT_LT(fd, 0);
    EXPECT_EQ(create_error, EROFS);
    fd = ::open("/individual", O_WRONLY | O_TRUNC);
    const int individual_error = errno;
    if (fd >= 0) ::close(fd);
    EXPECT_LT(fd, 0);
    EXPECT_EQ(individual_error, EROFS);
  }
  EXPECT_EQ(contents(source() / "file"), "original");
  EXPECT_FALSE(fs::exists(source() / "new"));
}

TEST_F(ScopedRootFilesystemTest, OmittedDestinationUsesTheSourcePath) {
  write_file(source() / "file", "same path");
  const auto path = source();
  auto scope = RootFilesystemFactory {root()}.bind(path).create();
  EXPECT_EQ(contents(path / "file"), "same path");
}

TEST_F(ScopedRootFilesystemTest, OverlappingBindsDoNotTruncateTheOriginal) {
  write_file(source() / "file", "must survive");
  {
    auto scope = RootFilesystemFactory {root()}
      .bind(source(), "/data")
      .bind(source() / "file", "/data/file").create();
    EXPECT_EQ(contents("/data/file"), "must survive");
  }
  EXPECT_EQ(contents(source() / "file"), "must survive");
}

TEST_F(ScopedRootFilesystemTest, ChildBindRegisteredBeforeParentIsNotShadowed) {
  write_file(source() / "dir/file", "original");
  write_file(source() / "override", "overridden");
  {
    auto scope = RootFilesystemFactory {root()}
      .bind(source() / "override", "/data/file")
      .bind(source() / "dir", "/data").create();
    EXPECT_EQ(contents("/data/file"), "overridden");
  }
  EXPECT_EQ(contents(source() / "dir/file"), "original");
}

TEST_F(ScopedRootFilesystemTest, MissingNamespaceDirectoryReportsSystemError) {
  fs::create_directories(source());
  {
    gh4ck3r::filesystem::ScopedRootDir chroot_guard {source()};
    EXPECT_THROW((void)RootFilesystemFactory {"/scoped"}.create(), std::system_error);
  }
  auto scope = RootFilesystemFactory {root()}.create();
}

TEST_F(ScopedRootFilesystemTest, RollsBackAPartiallyConstructedRoot) {
  write_file(source() / "file", "original");
  EXPECT_THROW((void)RootFilesystemFactory {root()}
      .bind(source(), "/readonly")
      .bind(source() / "missing", "/missing").create(), fs::filesystem_error);
  EXPECT_EQ(contents(source() / "file"), "original");
}

TEST_F(ScopedRootFilesystemTest, RootCreatedBeforePreparationIsNotOwnedOrRemoved) {
  gh4ck3r::filesystem::scoped_rootfs::detail::FilesystemContext context {source()};
  write_file(source() / "keep", "not owned");
  EXPECT_THROW(context.prepare(), std::invalid_argument);
  context.leave();
  context.restore();
  EXPECT_FALSE(context.remove_root());
  EXPECT_EQ(contents(source() / "keep"), "not owned");
}

TEST_F(ScopedRootFilesystemTest, RejectsDestinationSymlinksWithoutFollowingThem) {
  const auto canary = directory_->path() / "canary";
  write_file(canary, "do not truncate");
  fs::create_directories(source());
  fs::create_symlink(canary, source() / "alias");
  write_file(directory_->path() / "replacement", "replacement");
  EXPECT_THROW((void)RootFilesystemFactory {root()}
      .bind(source(), "/readonly")
      .bind(directory_->path() / "replacement", "/readonly/alias").create(),
      std::invalid_argument);
  EXPECT_EQ(contents(canary), "do not truncate");
}

TEST_F(ScopedRootFilesystemTest, RejectsNestedScopeOnSameThread) {
  auto outer = RootFilesystemFactory {root()}.create();
  write_file("/outer-marker", "outer");
  EXPECT_THROW((void)RootFilesystemFactory {"/inner"}.create(), std::logic_error);
  EXPECT_EQ(contents("/outer-marker"), "outer");
  EXPECT_FALSE(fs::exists("/inner"));
}

TEST_F(ScopedRootFilesystemTest, DirectoryBindsDoNotIncludeWritableSubmounts) {
  const auto child = source() / "child";
  write_file(child / "underlying", "underlying");
  ASSERT_EQ(::mount("tmpfs", child.c_str(), "tmpfs", 0, "size=1m"), 0);
  struct MountedChild {
    fs::path target;
    ~MountedChild() {
      if (::umount2(target.c_str(), 0) != 0)
        ADD_FAILURE() << "unmount test child: " << std::strerror(errno);
    }
  } mounted {child};
  write_file(child / "mounted", "mounted");
  {
    auto scope = RootFilesystemFactory {root()}.bind(source(), "/visible").create();
    EXPECT_EQ(contents("/visible/child/underlying"), "underlying");
    EXPECT_FALSE(fs::exists("/visible/child/mounted"));
  }
}

TEST_F(ScopedRootFilesystemTest, WorkerThreadScopeDoesNotChangeTheCallingThreadRoot) {
  const auto path = root();
  std::promise<void> entered;
  auto ready = entered.get_future();
  std::promise<void> release;
  auto finish = release.get_future();
  std::exception_ptr error;
  std::thread worker {[&] {
    bool signalled = false;
    try {
      auto scope = RootFilesystemFactory {path}.create();
      write_file("/worker-marker", "worker");
      entered.set_value();
      signalled = true;
      finish.wait();
    } catch (...) {
      error = std::current_exception();
      if (!signalled) entered.set_exception(error);
    }
  }};
  try {
    ready.get();
    EXPECT_EQ(fs::current_path(), cwd_);
    EXPECT_TRUE(fs::exists("/proc/thread-self/ns/mnt"));
    EXPECT_FALSE(fs::exists("/worker-marker"));
  } catch (...) {
    release.set_value();
    worker.join();
    throw;
  }
  release.set_value();
  worker.join();
  if (error) std::rethrow_exception(error);
}

class ScopedExecutableMockTest : public ScopedRootFilesystemTest {
 protected:
  void SetUp() override {
    ScopedRootFilesystemTest::SetUp();
    if (IsSkipped()) return;
    write_file(source() / "agent",
        "#!/bin/sh\nprintf 'original:%s\\n' \"$1\"\nexit 7\n");
    fs::permissions(source() / "agent", fs::perms::owner_all);
  }
};

TEST_F(ScopedExecutableMockTest, ForwardsArgumentsStdinBinaryOutputsAndExitStatus) {
  auto scope = RootFilesystemFactory {root()}
    .bind(source(), "/commands").mock("/commands/agent").create();
  EXPECT_CALL(scope.mock("/commands/agent"),
              execute(ElementsAre("/commands/agent", "with spaces", "", "line\nbreak"),
                      _, _, _))
    .WillOnce([](const auto &, int in, int out, int err) {
      std::array<char, 32> data {};
      const auto count = ::read(in, data.data(), data.size());
      EXPECT_EQ(std::string(data.data(), count > 0 ? count : 0), "stdin data");
      write_fd(out, std::string {"a\0b", 3});
      write_fd(err, "mock diagnostic\n");
      return 42;
    });
  const auto result = run_command("/commands/agent",
                                  {"with spaces", "", "line\nbreak"}, "stdin data");
  EXPECT_EQ(result.code, 42);
  EXPECT_EQ(result.output, (std::string {"a\0b", 3}));
  EXPECT_EQ(result.error, "mock diagnostic\n");
}

TEST_F(ScopedExecutableMockTest, CallbackOnceThenOriginalPreservesTheSource) {
  const auto original = contents(source() / "agent");
  {
    RootFilesystemFactory factory {root()};
    factory.bind(source(), "/commands").bind("/bin/sh").mock<::testing::NiceMock>("/commands/agent");
    bind_libraries(factory);
    auto scope = factory.create();
    auto &mock = scope.mock("/commands/agent");
    EXPECT_CALL(mock, execute(_, _, _, _)).Times(::testing::AnyNumber())
      .WillRepeatedly(Invoke(&mock, &Executable::execute_original));
    EXPECT_CALL(mock, execute(ElementsAre("/commands/agent", "argument"), _, _, _))
      .WillOnce(Return(19)).RetiresOnSaturation();
    EXPECT_EQ(run_command("/commands/agent", {"argument"}).code, 19);
    const auto native = run_command("/commands/agent", {"argument"});
    EXPECT_EQ(native.code, 7) << native.error;
    EXPECT_EQ(native.output, "original:argument\n");
    EXPECT_EQ(contents(mock.original_path()), original);
    int fd = ::open(mock.original_path().c_str(), O_WRONLY | O_TRUNC);
    const int error = errno;
    if (fd >= 0) ::close(fd);
    EXPECT_LT(fd, 0);
    EXPECT_EQ(error, EROFS);
  }
  EXPECT_EQ(contents(source() / "agent"), original);
}

TEST_F(ScopedExecutableMockTest, NiceMockDefaultsToTheOriginal) {
  RootFilesystemFactory factory {root()};
  factory.bind(source(), "/commands").bind("/bin/sh")
    .mock<::testing::NiceMock>("/commands/agent");
  bind_libraries(factory);
  auto scope = factory.create();
  const auto result = run_command("/commands/agent", {"default"});
  EXPECT_EQ(result.code, 7) << result.error;
  EXPECT_EQ(result.output, "original:default\n");
}

TEST_F(ScopedExecutableMockTest, SameBasenameAndRelativeInvocationRemainUnambiguous) {
  auto scope = RootFilesystemFactory {root()}
    .bind(source(), "/one").bind(source(), "/two")
    .mock("/one/agent").mock("/two/agent").create();
  EXPECT_CALL(scope.mock("/one/agent"), execute(ElementsAre("/one/agent"), _, _, _))
    .WillOnce(Return(11));
  EXPECT_CALL(scope.mock("/two/agent"), execute(ElementsAre("/two/agent"), _, _, _))
    .WillOnce(Return(22));
  fs::current_path("/one");
  EXPECT_EQ(run_command("./agent").code, 11);
  fs::current_path("/two");
  EXPECT_EQ(run_command("./agent").code, 22);
}

TEST_F(ScopedExecutableMockTest, CallerArgvZeroCannotSelectADifferentMock) {
  auto scope = RootFilesystemFactory {root()}
    .bind(source(), "/one").bind(source(), "/two")
    .mock("/one/agent").mock("/two/agent").create();
  EXPECT_CALL(scope.mock("/one/agent"), execute(ElementsAre("/one/agent"), _, _, _))
    .WillOnce(Return(11));
  EXPECT_CALL(scope.mock("/two/agent"), execute(_, _, _, _)).Times(0);
  EXPECT_EQ(run_command("/one/agent", {}, {}, "/two/agent").code, 11);
}

TEST_F(ScopedExecutableMockTest, NativeExecutionPreservesCallerArgvZero) {
  RootFilesystemFactory factory {root()};
  factory.bind("/bin/sh", "/commands/shell")
    .mock<::testing::NiceMock>("/commands/shell");
  bind_libraries(factory);
  auto scope = factory.create();
  const auto result = run_command("/commands/shell", {"-c", "printf '%s' \"$0\""},
                                   {}, "caller-shell");
  EXPECT_EQ(result.code, 0) << result.error;
  EXPECT_EQ(result.output, "caller-shell");
}

TEST_F(ScopedExecutableMockTest, MissingServerFailsClosedInsteadOfRunningTestMain) {
  auto scope = RootFilesystemFactory {root()}
    .bind(source(), "/commands").mock("/commands/agent").create();
  EXPECT_CALL(scope.mock("/commands/agent"), execute(_, _, _, _)).Times(0);
  ASSERT_EQ(::unlink("/.mock_ipc/sock"), 0);
  const auto result = run_command("/commands/agent");
  EXPECT_EQ(result.code, 125);
  EXPECT_TRUE(result.output.empty());
  EXPECT_EQ(result.error, "mock command transport failed\n");
}

TEST_F(ScopedExecutableMockTest, CallbackExceptionIsReportedAfterFilesystemRestoration) {
  EXPECT_NONFATAL_FAILURE({
    auto scope = RootFilesystemFactory {root()}
      .bind(source(), "/commands").mock("/commands/agent").create();
    EXPECT_CALL(scope.mock("/commands/agent"), execute(_, _, _, _))
      .WillOnce([](const auto &, int, int, int) -> int {
        throw std::runtime_error {"injected callback exception"};
      });
    const auto result = run_command("/commands/agent");
    EXPECT_EQ(result.code, 125);
  }, "injected callback exception");
  EXPECT_EQ(fs::current_path(), cwd_);
  EXPECT_FALSE(fs::exists(root()));
}

TEST_F(ScopedExecutableMockTest, RepeatedScopesDoNotRetainMockActions) {
  for (int i = 0; i < 2; ++i) {
    auto scope = RootFilesystemFactory {root()}
      .bind(source(), "/commands").mock("/commands/agent").create();
    EXPECT_CALL(scope.mock("/commands/agent"), execute(_, _, _, _))
      .WillOnce(Return(20 + i));
    EXPECT_EQ(run_command("/commands/agent").code, 20 + i);
  }
}

TEST_F(ScopedExecutableMockTest, UnregisteredExecutableDoesNotBecomeAMock) {
  auto scope = RootFilesystemFactory {root()}
    .bind(source(), "/commands").mock("/commands/agent").create();
  EXPECT_CALL(scope.mock("/commands/agent"), execute(_, _, _, _)).Times(0);
  const auto result = run_command("/.mock_ipc/stub",
      {"--gtest_list_tests", "--gtest_filter=ScopedRootFilesystemValidation.*"});
  EXPECT_EQ(result.code, 0) << result.error;
  EXPECT_NE(result.output.find("ScopedRootFilesystemValidation."), std::string::npos);
  EXPECT_TRUE(result.error.empty());
}

TEST_F(ScopedExecutableMockTest, MalformedClientIsReportedWithoutLosingRestoration) {
  EXPECT_NONFATAL_FAILURE({
    auto scope = RootFilesystemFactory {root()}
      .bind(source(), "/commands").mock("/commands/agent").create();
    EXPECT_CALL(scope.mock("/commands/agent"), execute(_, _, _, _)).Times(0);
    unique_fd client {::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, "/.mock_ipc/sock", sizeof(address.sun_path) - 1);
    EXPECT_EQ(::connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
    EXPECT_TRUE(gh4ck3r::filesystem::scoped_rootfs::mock::set_timeouts(client));
    const auto header = (gh4ck3r::filesystem::scoped_rootfs::mock::RequestHeader {
      gh4ck3r::filesystem::scoped_rootfs::mock::protocol_magic, 1, 1});
    EXPECT_TRUE(gh4ck3r::filesystem::scoped_rootfs::mock::send_all(client, &header, sizeof(header)));
    gh4ck3r::filesystem::scoped_rootfs::mock::Response response;
    EXPECT_TRUE(gh4ck3r::filesystem::scoped_rootfs::mock::read_all(client, &response, sizeof(response)));
    EXPECT_EQ(response.code, 125);
  }, "invalid mock IPC descriptors");
}

TEST_F(ScopedExecutableMockTest, MissingMockTargetRollsBackAllBindings) {
  EXPECT_THROW((void)RootFilesystemFactory {root()}.bind(source(), "/commands")
      .mock("/commands/missing").create(), std::invalid_argument);
  EXPECT_EQ(contents(source() / "agent"),
            "#!/bin/sh\nprintf 'original:%s\\n' \"$1\"\nexit 7\n");
}

TEST_F(ScopedExecutableMockTest, MockConstructionFailureRestoresFilesystemAndAllowsAnotherScope) {
  const auto original = contents(source() / "agent");
  EXPECT_THROW((void)RootFilesystemFactory {root()}.bind(source(), "/commands")
      .mock<FailingMock>("/commands/agent").create(), std::runtime_error);
  EXPECT_EQ(fs::current_path(), cwd_);
  EXPECT_FALSE(fs::exists(root()));
  EXPECT_EQ(contents(source() / "agent"), original);
  struct stat current {};
  ASSERT_EQ(::stat("/proc/thread-self/ns/mnt", &current), 0);
  EXPECT_EQ(current.st_dev, namespace_.st_dev);
  EXPECT_EQ(current.st_ino, namespace_.st_ino);
  auto scope = RootFilesystemFactory {root()}.create();
  EXPECT_EQ(fs::current_path(), "/");
}

TEST_F(ScopedExecutableMockTest, CustomDirectoryBoundOverRuntimeLibraryPrefixSupportsMocks) {
  auto scope = RootFilesystemFactory {root()}
    .bind(source(), "/usr").mock("/usr/agent").create();
  EXPECT_FALSE(fs::exists("/usr/lib"));
  EXPECT_CALL(scope.mock("/usr/agent"), execute(ElementsAre("/usr/agent", "ok"), _, _, _))
    .WillOnce(Return(37));
  EXPECT_EQ(run_command("/usr/agent", {"ok"}).code, 37);
}

TEST_F(ScopedExecutableMockTest, NestedMockExecutionInsideCallbackDoesNotDeadlock) {
  auto scope = RootFilesystemFactory {root()}
    .bind(source(), "/one").bind(source(), "/two")
    .mock("/one/agent").mock("/two/agent").create();
  EXPECT_CALL(scope.mock("/two/agent"),
              execute(ElementsAre("/two/agent", "from-outer"), _, _, _))
    .WillOnce([](const auto &, int, int out, int) {
      write_fd(out, "inner-output");
      return 15;
    });
  EXPECT_CALL(scope.mock("/one/agent"), execute(ElementsAre("/one/agent"), _, _, _))
    .WillOnce([](const auto &, int, int out, int) {
      const auto inner = run_command("/two/agent", {"from-outer"});
      write_fd(out, inner.output);
      return inner.code + 10;
    });
  const auto result = run_command("/one/agent");
  EXPECT_EQ(result.code, 25);
  EXPECT_EQ(result.output, "inner-output");
}

TEST_F(ScopedExecutableMockTest, ConcurrentPipelineBetweenMockedCommandsDoesNotDeadlock) {
  auto scope = RootFilesystemFactory {root()}
    .bind(source(), "/one").bind(source(), "/two")
    .mock("/one/agent").mock("/two/agent").create();
  std::promise<void> consumer_entered;
  auto consumer_ready = consumer_entered.get_future().share();
  EXPECT_CALL(scope.mock("/two/agent"), execute(ElementsAre("/two/agent"), _, _, _))
    .WillOnce([consumer_entered = std::move(consumer_entered)](const auto &, int in, int out, int) mutable {
      consumer_entered.set_value();
      std::array<char, 64> buffer {};
      const auto count = ::read(in, buffer.data(), buffer.size());
      if (count > 0) write_fd(out, std::string(buffer.data(), static_cast<std::size_t>(count)));
      return 20;
    });
  EXPECT_CALL(scope.mock("/one/agent"), execute(ElementsAre("/one/agent"), _, _, _))
    .WillOnce([consumer_ready](const auto &, int, int out, int) {
      consumer_ready.wait();
      write_fd(out, "piped payload");
      return 10;
    });
  auto consumer_future = std::async(std::launch::async, [&] {
    return run_command("/two/agent", {}, "piped payload");
  });
  consumer_ready.wait();
  const auto producer_result = run_command("/one/agent");
  const auto consumer_result = consumer_future.get();
  EXPECT_EQ(producer_result.code, 10);
  EXPECT_EQ(producer_result.output, "piped payload");
  EXPECT_EQ(consumer_result.code, 20);
  EXPECT_EQ(consumer_result.output, "piped payload");
}

TEST_F(ScopedExecutableMockTest, ServerContinuesHandlingMocksAfterCallbackException) {
  EXPECT_NONFATAL_FAILURE({
    auto scope = RootFilesystemFactory {root()}
      .bind(source(), "/commands").mock("/commands/agent").create();
    EXPECT_CALL(scope.mock("/commands/agent"), execute(_, _, _, _))
      .WillOnce([](const auto &, int, int, int) -> int {
        throw std::runtime_error {"first invocation failed"};
      })
      .WillOnce([](const auto &, int, int out, int) {
        write_fd(out, "recovered");
        return 55;
      });
    EXPECT_EQ(run_command("/commands/agent").code, 125);
    const auto second = run_command("/commands/agent");
    EXPECT_EQ(second.code, 55);
    EXPECT_EQ(second.output, "recovered");
  }, "first invocation failed");
}

TEST_F(ScopedExecutableMockTest, ConcurrentOriginalAndCustomActionsDoNotRaceOnMockState) {
  RootFilesystemFactory factory {root()};
  factory.bind(source(), "/commands").bind("/bin/sh")
    .mock<::testing::NiceMock>("/commands/agent");
  bind_libraries(factory);
  auto scope = factory.create();
  auto &mock = scope.mock("/commands/agent");

  std::promise<void> custom_entered;
  auto custom_ready = custom_entered.get_future().share();
  std::promise<void> original_called;
  auto allow_custom_return = original_called.get_future().share();

  EXPECT_CALL(mock, execute(ElementsAre("/commands/agent", "native"), _, _, _))
    .WillOnce([&](const auto &argv, int in, int out, int err) {
      const int rc = mock.execute_original(argv, in, out, err);
      original_called.set_value();
      return rc;
    });
  EXPECT_CALL(mock, execute(ElementsAre("/commands/agent", "custom"), _, _, _))
    .WillOnce([&](const auto &, int, int, int) {
      custom_entered.set_value();
      allow_custom_return.wait();
      return 33;
    });

  auto custom_future = std::async(std::launch::async, [] {
    return run_command("/commands/agent", {"custom"});
  });
  custom_ready.wait();
  const auto native_result = run_command("/commands/agent", {"native"});
  const auto custom_result = custom_future.get();
  EXPECT_EQ(native_result.code, 7) << native_result.error;
  EXPECT_EQ(native_result.output, "original:native\n");
  EXPECT_EQ(custom_result.code, 33);
}

} // namespace
