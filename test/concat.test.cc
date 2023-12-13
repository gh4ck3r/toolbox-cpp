#include "gh4ck3r/concat.hh"
#include <gtest/gtest.h>

using std::operator""sv;
using gh4ck3r::concat;

TEST(concat, string_view)
{
  constexpr static std::string_view foo {"foo"}, bar {"bar"}, baz {"baz"};
  constexpr auto newsv {concat<foo, bar, baz>()};

  EXPECT_EQ("foobarbaz"sv, newsv);
  EXPECT_EQ(0x00, *newsv.end());
}

TEST(concat, array)
{
  constexpr std::array foo {1,2,3};
  constexpr std::array bar {4,5,6};
  constexpr std::array baz {1,2,3,4,5,6};
  EXPECT_EQ(baz, concat(foo, bar));
}
