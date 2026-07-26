#include <fcntl.h>
#include "gh4ck3r/reaper.hh"
#include <gmock/gmock.h>

using gh4ck3r::Reaper;

TEST(Reaper, fopen)
{
  Reaper file {std::fopen(__FILE__, "r")};
  ASSERT_NE(file, nullptr);

  char buf[8 + 1];
  buf[fread(buf, 1, sizeof(buf) - 1, file)] = 0x00;
  EXPECT_STREQ(buf, "#include") << buf;
}

TEST(Reaper, fopen_move)
{
  auto f = std::fopen(__FILE__, "r");
  ASSERT_NE(f, nullptr);

  Reaper file = std::move(f);
  EXPECT_EQ(f, nullptr);
  EXPECT_TRUE(file);

  char buf[8 + 1];
  buf[fread(buf, 1, sizeof(buf) - 1, file)] = 0x00;
  EXPECT_STREQ(buf, "#include") << buf;
}

TEST(Reaper, fopen_release)
{
  Reaper file {std::fopen(__FILE__, "r")};
  ASSERT_NE(file, nullptr);

  auto fp = file.release();
  EXPECT_FALSE(file);
  EXPECT_NE(fp, nullptr);
  EXPECT_EQ(std::fclose(fp), 0);
}

TEST(Reaper, malloc)
{
  Reaper<std::free> p = std::malloc(128);
  EXPECT_TRUE(p);

  sprintf(p, "hello, world");
  EXPECT_EQ(std::string_view{p}, "hello, world");
}

TEST(Reaper, malloc_move)
{
  void * rawp = std::malloc(128);
  ASSERT_NE(rawp, nullptr);

  Reaper<std::free> p = std::move(rawp);
  EXPECT_EQ(rawp, nullptr);
  EXPECT_TRUE(p);
}

TEST(Reaper, malloc_reassign)
{
  Reaper<std::free> p = std::malloc(128);
  EXPECT_TRUE(p);

  void *rawp = std::malloc(64);
  p = std::move(rawp);
  EXPECT_TRUE(p);
  EXPECT_EQ(rawp, nullptr);
}

TEST(Reaper, open)
{
  ASSERT_EQ(::open(__FILE__, O_DIRECTORY), -1);
  EXPECT_THROW(Reaper<::close>{::open(__FILE__, O_DIRECTORY)},
               std::system_error);
}

