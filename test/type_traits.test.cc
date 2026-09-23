#include <gh4ck3r/type_traits.hh>
#include <gtest/gtest.h>

namespace gh4ck3r::metatype {

TEST(type_traitsTest, is_complete)
{
  struct foo;     // declared
  static_assert(!is_complete_v<foo>);

  struct bar {};  // defined
  static_assert(is_complete_v<bar>);

  struct baz;     // declare
  static_assert(!is_complete_v<baz>); // set to false
  struct baz {};  // defined
  static_assert(!is_complete_v<baz>); // gotcha: already set to false
}

TEST(type_traitsTest, is_complete_fundamental_and_void)
{
  static_assert(is_complete_v<int>);
  static_assert(is_complete_v<double>);
  static_assert(is_complete_v<char>);

  static_assert(!is_complete_v<void>);
  static_assert(!is_complete_v<const void>);
  static_assert(!is_complete_v<volatile void>);
}

TEST(type_traitsTest, is_complete_arrays)
{
  static_assert(is_complete_v<int[10]>);
  static_assert(!is_complete_v<int[]>);
}

} // namespace gh4ck3r::metatype
