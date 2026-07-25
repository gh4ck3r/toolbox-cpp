#include <cstdio>
#include "gh4ck3r/function_traits.hh"
#include <gtest/gtest.h>

namespace gh4ck3r::metatype {

TEST(function_traitsTest, return_type)
{
  static_assert(std::is_same_v<function_trait<std::fopen>::return_type, FILE*>);

  static_assert(std::is_same_v<function_trait<std::fclose>::return_type, int>);

  void foo();
  static_assert(std::is_same_v<function_trait<foo>::return_type, void>);
}

TEST(function_traitsTest, arguments)
{
  using fopen_trait = function_trait<std::fopen>;
  static_assert(fopen_trait::arity == 2);
  static_assert(std::is_same_v<fopen_trait::return_type, FILE*>);
  static_assert(std::is_same_v<fopen_trait::first_argument_type, const char*>);
  static_assert(std::is_same_v<fopen_trait::last_argument_type, const char*>);

  using fclose_trait = function_trait<std::fclose>;
  static_assert(fclose_trait::arity == 1);
  static_assert(std::is_same_v<fclose_trait::first_argument_type, FILE*>);
  static_assert(std::is_same_v<fclose_trait::last_argument_type, FILE*>);

  void foo();
  using foo_trait = function_trait<foo>;
  static_assert(foo_trait::arity == 0);
  static_assert(std::is_same_v<foo_trait::first_argument_type, void>);
  static_assert(std::is_same_v<foo_trait::last_argument_type, void>);
}

} // namespace gh4ck3r::metatype
