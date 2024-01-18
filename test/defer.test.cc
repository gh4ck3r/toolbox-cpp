#include "gh4ck3r/defer.hh"
#include <gtest/gtest.h>

using gh4ck3r::Defer;

TEST(defer, basic)
{
  bool v = false;
  {
    Defer _ {[&] {v = true;}};
    EXPECT_FALSE(v);
  }
  EXPECT_TRUE(v);
}

TEST(defer, assign)
{
  int v = 0;
  {
    Defer _ {[&] {v = 1;}};
    _ = [&] {v = 2;};
    EXPECT_EQ(0, v);
  }
  EXPECT_EQ(2, v);
}

TEST(defer, assign_lvalue)
{
  int v = 0;
  {
    EXPECT_EQ(0, v);
    Defer _ {[&] {v = 1;}};
    const auto func = [&] {v = 2;};
    _ = func;
    EXPECT_EQ(0, v);
  }
  EXPECT_EQ(2, v);
}

TEST(defer, release)
{
  int v = 0;
  {
    Defer _ {[&] {v = 1;}};
    _.release();
  }
  EXPECT_EQ(0, v);
}
