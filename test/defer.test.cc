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

TEST(defer, release)
{
  int v = 0;
  {
    Defer _ {[&] {v = 1;}};
    _.release();
  }
  EXPECT_EQ(0, v);
}

TEST(defer, nullptr_assignment)
{
  int v = 0;
  {
    Defer _ {[&] {v = 1;}};
    _ = nullptr;
  }
  EXPECT_EQ(0, v);
}

TEST(defer, invoke)
{
  int v = 0;
  {
    Defer _ {[&] {++v;}};
    EXPECT_EQ(0, v);
    _();
    EXPECT_EQ(1, v);
  }
  EXPECT_EQ(1, v);
}

TEST(defer, move_construction)
{
  int count = 0;
  {
    Defer d1 {[&] { ++count; }};
    Defer d2 {std::move(d1)};
  }
  EXPECT_EQ(1, count);
}
