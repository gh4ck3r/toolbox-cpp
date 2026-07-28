#include "gh4ck3r/split.hh"
#include <gtest/gtest.h>

namespace gh4ck3r {

TEST(split, string)
{
  const std::string str {"1,2,3,4"};
  const auto values = split<','>(str);
  EXPECT_EQ(values.size(), 4);
  EXPECT_EQ(values[0], "1");
  EXPECT_EQ(values[1], "2");
  EXPECT_EQ(values[2], "3");
  EXPECT_EQ(values[3], "4");
}

TEST(split, string_view)
{
  const auto values = split<'|'>("1|2|3|4");
  ASSERT_EQ(values.size(), 4);
  EXPECT_EQ(values[0], "1");
  EXPECT_EQ(values[1], "2");
  EXPECT_EQ(values[2], "3");
  EXPECT_EQ(values[3], "4");
}

TEST(split, empty_str)
{
  constexpr std::string_view str {};
  const auto values = split<','>(str);
  EXPECT_EQ(values.size(), 1);
  EXPECT_EQ(values.front(), str);
}

TEST(split, null_sep)
{
  constexpr std::string_view str {"1,2,3,4"};
  const auto values = split<0x00>(str);
  EXPECT_EQ(values.size(), 1);
  EXPECT_EQ(values.front(), str);
}

TEST(split, beginning_sep)
{
  constexpr std::string_view str {",1,2,3,4"};
  const auto values = split<','>(str);
  EXPECT_EQ(values.size(), 5);
  EXPECT_TRUE(values.front().empty());
  EXPECT_EQ(values[1], "1");
  EXPECT_EQ(values[2], "2");
  EXPECT_EQ(values[3], "3");
  EXPECT_EQ(values[4], "4");
}

TEST(split, ending_sep)
{
  constexpr std::string_view str {"1,2,3,4,"};
  const auto values = split<','>(str);
  EXPECT_EQ(values.size(), 5);
  EXPECT_EQ(values[0], "1");
  EXPECT_EQ(values[1], "2");
  EXPECT_EQ(values[2], "3");
  EXPECT_EQ(values[3], "4");
  EXPECT_TRUE(values.back().empty());
}

TEST(split, multi_sep)
{
  const auto values = split<','>("1,2,3,4|5/6/7/8");
  ASSERT_EQ(values.size(), 4);
  EXPECT_EQ(values[0], "1");
  EXPECT_EQ(values[1], "2");
  EXPECT_EQ(values[2], "3");
  EXPECT_EQ(values[3], "4|5/6/7/8");

  const auto values2 = split<'|'>(values.back());
  ASSERT_EQ(values2.size(), 2);
  EXPECT_EQ(values2.front(), "4");
  EXPECT_EQ(values2.back(), "5/6/7/8");

  const auto values3 = split<'/'>(values2.back());
  ASSERT_EQ(values3.size(), 4);
  EXPECT_EQ(values3[0], "5");
  EXPECT_EQ(values3[1], "6");
  EXPECT_EQ(values3[2], "7");
  EXPECT_EQ(values3[3], "8");
}

TEST(split, escape_sep)
{
  const auto values = split<',', '\\'>(R"(1,2,3\,4)");
  ASSERT_EQ(values.size(), 3);
  EXPECT_EQ(values[0], "1");
  EXPECT_EQ(values[1], "2");
  EXPECT_EQ(values[2], R"(3\,4)");
}

TEST(split, escape_sep1)
{
  const auto values = split<',', '\\'>(R"(1,2,3,4)");
  ASSERT_EQ(values.size(), 4);
  EXPECT_EQ(values[0], "1");
  EXPECT_EQ(values[1], "2");
  EXPECT_EQ(values[2], "3");
  EXPECT_EQ(values[3], "4");
}

TEST(split, escape_sep2)
{
  const auto values = split<',', '\\'>(R"(1,2,3\\,4)");
  ASSERT_EQ(values.size(), 4);
  EXPECT_EQ(values[0], "1");
  EXPECT_EQ(values[1], "2");
  EXPECT_EQ(values[2], R"(3\\)");
  EXPECT_EQ(values[3], "4");
}

TEST(split, escape_sep3)
{
  const auto values = split<',', '\\'>(R"(\1,2\\,3\\\,4\)");
  ASSERT_EQ(values.size(), 3);
  EXPECT_EQ(values[0], R"(\1)");
  EXPECT_EQ(values[1], R"(2\\)");
  EXPECT_EQ(values[2], R"(3\\\,4\)");
}

} //  namespace gh4ck3r
