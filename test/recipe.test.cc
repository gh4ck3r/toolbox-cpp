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
