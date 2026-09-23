#include <string>
#include "gh4ck3r/recipe.hh"
#include <gtest/gtest.h>

using gh4ck3r::recipe;

TEST(recipe, basic)
{
  std::string fp {"0"};
  EXPECT_EQ("0", fp);
  const auto r = recipe(
      [&] (auto ...params) { fp += "1"; return (0 + ... + params); }
      ,[&] (auto arg) { fp += "2"; return arg * 3; }
      ,[&] (auto arg) { fp += "3"; return arg / 5; }
  );
  EXPECT_EQ("0", fp);
  // (1 +  ... + 10) * 3 / 5
  EXPECT_EQ(33, r(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
  EXPECT_EQ(fp, "0123");
}

TEST(recipe, move_only_callable)
{
  auto ptr = std::make_unique<int>(10);
  auto r = recipe(
    [p = std::move(ptr)](int x) { return *p + x; },
    [](int x) { return x * 2; }
  );
  EXPECT_EQ(40, r(10));
}

TEST(recipe, perfect_forwarding)
{
  struct NonCopyable {
    int val;
    NonCopyable(int v) : val(v) {}
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
  };

  auto r = recipe(
    [](NonCopyable nc) { return nc.val + 5; },
    [](int x) { return std::to_string(x); }
  );

  EXPECT_EQ("15", r(NonCopyable{10}));
}
