#pragma once

#include "scoped_rootfs.hh"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <linux/loop.h>
#include <linux/major.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>

namespace test_support {

// Construct before entering the scoped root; destroy before leaving it.
class ConfigStorage {
 public:
  explicit ConfigStorage(std::filesystem::path image_path)
      : image_path_(std::move(image_path)),
        control_(open_file("/dev/loop-control", O_RDWR))
  {
    FormatImage();
  }

  ConfigStorage(const ConfigStorage &) = delete;
  ConfigStorage &operator=(const ConfigStorage &) = delete;

  ~ConfigStorage() noexcept
  {
    try {
      Restore();
    } catch (const std::exception &error) {
      std::fprintf(stderr, "restore test storage: %s\n", error.what());
      std::abort();
    }
  }

  void Enter()
  {
    if (dev_mounted_)
      throw std::logic_error {"test storage is already mounted"};
    if (::mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_NOEXEC,
                "size=1m,mode=0755") != 0)
      system_error("mount private test device directory");
    dev_mounted_ = true;
    if (::mknod("/dev/null", S_IFCHR | 0666, makedev(MEM_MAJOR, 3)) != 0)
      system_error("create private null device");
    AttachLoop();
    control_.reset();
    if (::mount("/dev/gtest3", "/storage", "ext4",
                MS_NOSUID | MS_NODEV, nullptr) != 0)
      system_error("mount test filesystem inspection view");
    inspection_mounted_ = true;
  }

  bool mounted() const noexcept { return inspection_mounted_; }

 private:
  [[noreturn]] static void system_error(const std::string &operation, int error = errno)
  {
    throw std::system_error {error, std::system_category(), operation};
  }

  static gh4ck3r::filesystem::unique_fd open_file(
      const std::filesystem::path &path, int flags)
  {
    const int fd = ::open(path.c_str(), flags | O_CLOEXEC, 0600);
    if (fd < 0)
      system_error("open test storage " + path.string());
    return {fd};
  }

  void FormatImage()
  {
    image_.emplace(open_file(image_path_, O_RDWR | O_CREAT | O_EXCL));
    if (::ftruncate(*image_, 64 * 1024 * 1024) != 0)
      system_error("create test filesystem image");
    std::array<std::string, 6> arguments {{
        "mke2fs", "-q", "-F", "-t", "ext4", image_path_.string(),
    }};
    std::array<char *, 7> args {};
    for (std::size_t i = 0; i < arguments.size(); ++i)
      args[i] = arguments[i].data();
    pid_t pid;
    const int error = ::posix_spawnp(&pid, args[0], nullptr, nullptr,
                                    args.data(), environ);
    if (error != 0)
      system_error("start test filesystem formatter", error);
    int status;
    while (::waitpid(pid, &status, 0) < 0) {
      if (errno != EINTR)
        system_error("wait for test filesystem formatter");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      throw std::runtime_error {"mke2fs failed for test filesystem image"};
  }

  void AttachLoop()
  {
    for (int attempt = 0; attempt < 16; ++attempt) {
      const int index = ::ioctl(*control_, LOOP_CTL_GET_FREE);
      if (index < 0)
        system_error("allocate test loop device");
      if (::mknod("/dev/gtest3", S_IFBLK | 0600, makedev(LOOP_MAJOR, index)) != 0)
        system_error("create test loop device node");
      loop_.emplace(open_file("/dev/gtest3", O_RDWR));
      if (::ioctl(*loop_, LOOP_SET_FD, static_cast<int>(*image_)) == 0) {
        loop_attached_ = true;
        loop_info64 info {};
        info.lo_flags = LO_FLAGS_AUTOCLEAR;
        std::snprintf(reinterpret_cast<char *>(info.lo_file_name),
                      sizeof(info.lo_file_name), "%s", image_path_.c_str());
        if (::ioctl(*loop_, LOOP_SET_STATUS64, &info) != 0)
          system_error("configure test loop device");
        return;
      }
      const int error = errno;
      loop_.reset();
      if (::unlink("/dev/gtest3") != 0)
        system_error("remove unused test loop node");
      if (error != EBUSY)
        system_error("attach test loop image", error);
    }
    throw std::runtime_error {"could not reserve a test loop device after 16 attempts"};
  }

  void Restore()
  {
    if (dev_mounted_ && ::umount2("/mnt", 0) != 0 &&
        errno != EINVAL && errno != ENOENT)
      system_error("unmount remaining test command mount");
    if (inspection_mounted_) {
      if (::umount2("/storage", 0) != 0)
        system_error("unmount test filesystem inspection view");
      inspection_mounted_ = false;
    }
    if (loop_attached_) {
      if (::ioctl(*loop_, LOOP_CLR_FD) != 0)
        system_error("detach test loop image");
      loop_attached_ = false;
    }
    loop_.reset();
    image_.reset();
    control_.reset();
    if (dev_mounted_) {
      if (::umount2("/dev", 0) != 0)
        system_error("unmount private test device directory");
      dev_mounted_ = false;
    }
  }

  std::filesystem::path image_path_;
  std::optional<gh4ck3r::filesystem::unique_fd> control_;
  std::optional<gh4ck3r::filesystem::unique_fd> image_;
  std::optional<gh4ck3r::filesystem::unique_fd> loop_;
  bool loop_attached_ = false;
  bool dev_mounted_ = false;
  bool inspection_mounted_ = false;
};

} // namespace test_support
