#include "gh4ck3r/logger.hh"
#include <gtest/gtest.h>
#include <string>

using gh4ck3r::Logger;
using gh4ck3r::indent;
using gh4ck3r::unindent;
using std::operator""s;

TEST(logger, basic)
{
  std::ostringstream oss;
  Logger logger{oss};

  const auto expected {
R"(begin
  hello
end)"};

  logger << "begin" << '\n'
    << indent()
    << "hello\n"
    << unindent()
    << "end";

  EXPECT_EQ(expected, oss.str());
}

TEST(logger, indent)
{
  std::ostringstream oss;
  Logger logger{oss};

  const auto data {"hello world"};
  const std::string spaces {"  "};

  logger << indent();
  // indentation performs on first character as of lines
  EXPECT_TRUE(oss.str().empty());

  logger << data;
  auto expected {"  hello world"};
  EXPECT_EQ(expected, oss.str());

  logger << unindent();
  // unindentation performs on same as indentation
  EXPECT_EQ(expected, oss.str());

  logger << data;
  expected = "  hello worldhello world";
  EXPECT_EQ(expected, oss.str());

  logger << '\n' << data;
  expected = "  hello worldhello world\nhello world";
  EXPECT_EQ(expected, oss.str());
}

TEST(logger, indent_multi)
{
  std::ostringstream oss;
  Logger logger{oss};

  logger << "begin\n"
    << indent()
    << "first\n"
    << indent()
    << "second\n"
    << indent(2)
    << "fourth\n"
    << unindent()
    << "third\n"
    << unindent(2)
    << "end";

  const auto expected {R"(begin
  first
    second
        fourth
      third
  end)"};
  EXPECT_EQ(expected, oss.str());
}
