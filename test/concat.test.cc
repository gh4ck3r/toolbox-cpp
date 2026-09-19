#include "gh4ck3r/concat.hh"
#include <gtest/gtest.h>

using std::operator""sv;
using gh4ck3r::concat;
using gh4ck3r::concat_sep;

TEST(concat, string_view)
{
  constexpr static std::string_view foo {"foo"}, bar {"bar"}, baz {"baz"};
  constexpr auto newsv {concat<foo, bar, baz>()};

  EXPECT_EQ("foobarbaz"sv, newsv);
  EXPECT_EQ(0x00, *newsv.end());
}

TEST(concat, string_view_sep)
{
  constexpr static std::string_view foo {"foo"}, bar {"bar"}, baz {"baz"};
  constexpr auto joined {concat_sep<',', foo, bar, baz>()};

  EXPECT_EQ("foo,bar,baz"sv, joined);
  EXPECT_EQ(0x00, *joined.end());

  constexpr auto joined_detail {gh4ck3r::detail::concat_sv<',', foo, bar, baz>::value};
  EXPECT_EQ("foo,bar,baz"sv, joined_detail);
}

TEST(concat, string_view_empty)
{
  constexpr auto empty_sv {concat<>()};
  EXPECT_EQ(""sv, empty_sv);
  EXPECT_EQ(0, empty_sv.size());
}

TEST(concat, array)
{
  constexpr std::array foo {1, 2, 3};
  constexpr std::array bar {4, 5, 6};
  constexpr std::array expected {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(expected, concat(foo, bar));
}

TEST(concat, array_single)
{
  constexpr std::array foo {1, 2, 3};
  EXPECT_EQ(foo, concat(foo));
}

TEST(concat, array_multiple)
{
  constexpr std::array a {1, 2};
  constexpr std::array b {3, 4};
  constexpr std::array c {5, 6};
  constexpr std::array expected {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(expected, concat(a, b, c));
}

// Compile-time static assertions
static_assert(concat<>().empty());
constexpr std::array g_arr1 {10, 20};
constexpr std::array g_arr2 {30, 40};
static_assert(concat(g_arr1, g_arr2).size() == 4);
