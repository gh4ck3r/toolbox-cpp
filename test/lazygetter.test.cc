#include "gh4ck3r/lazygetter.hh"
#include <gtest/gtest.h>

using gh4ck3r::LazyGetter;

TEST(LazyGetter, primitive)
{
  size_t cnt {};
  constexpr int val = 10;
  LazyGetter getter { [&cnt] { cnt++; return val; } };
  EXPECT_EQ(0, cnt);
  EXPECT_EQ(val, getter);
  EXPECT_EQ(1, cnt);
  EXPECT_EQ(val + 1, ++getter);
  EXPECT_EQ(1, cnt);
  EXPECT_EQ(val, --getter);
  EXPECT_EQ(1, cnt);
  EXPECT_EQ(val>>1, getter>>=1);
  EXPECT_EQ(1, cnt);
  EXPECT_EQ(val, getter<<=1);
  EXPECT_EQ(1, cnt);
}

TEST(LazyGetter, primitive_assign)
{
  size_t cnt {};
  constexpr int val = 10;
  LazyGetter getter {[&cnt] { cnt++; return val; }};
  EXPECT_EQ(0, cnt);
  getter = val / 2;
  EXPECT_EQ(5, getter);
  EXPECT_EQ(0, cnt);
}

TEST(LazyGetter, string)
{
  const std::string val {"hello world"};
  size_t cnt {};
  LazyGetter getter {[&] { cnt++; return val; }};
  EXPECT_EQ(0, cnt);
  EXPECT_EQ(val, getter);
  EXPECT_EQ(1, cnt);
  EXPECT_EQ(val, getter);
  EXPECT_EQ(1, cnt);
}

TEST(LazyGetter, string_view)
{
  const std::string_view val {"hello world"};
  size_t cnt {};
  LazyGetter getter {[&] { cnt++; return val; }};
  EXPECT_EQ(0, cnt);
  EXPECT_EQ(val, getter);
  EXPECT_EQ(1, cnt);
  EXPECT_EQ(val, getter);
  EXPECT_EQ(1, cnt);
}
