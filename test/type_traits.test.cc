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

} // namespace gh4ck3r::metatype
