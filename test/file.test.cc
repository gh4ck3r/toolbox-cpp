#include "gh4ck3r/file.hh"
#include <gtest/gtest.h>

#include <fcntl.h>

class unique_fd_Test : public ::testing::Test {
 protected:
  using unique_fd = gh4ck3r::file::unique_fd;
};

TEST_F(unique_fd_Test, basic)
{
  const int fd = ::open(__FILE__, O_RDONLY);
  ASSERT_NE(-1, fd);

  {
    unique_fd ufd { fd };
    EXPECT_EQ(fd, ufd);
    EXPECT_TRUE(ufd);
  }

  struct stat st;
  EXPECT_EQ(-1, fstat(fd, &st));
  EXPECT_EQ(EBADF, errno);
}
