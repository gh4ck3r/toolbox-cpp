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
  static_assert(std::is_same_v<function_trait<std::free>::return_type, void>);
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

  using free_trait = function_trait<std::free>;
  static_assert(free_trait::arity == 1);
  static_assert(std::is_same_v<free_trait::first_argument_type, void*>);
}

static void noexcept_func(int, double) noexcept {}

TEST(function_traitsTest, noexcept_function)
{
  using trait = function_trait<noexcept_func>;
  static_assert(trait::arity == 2);
  static_assert(std::is_same_v<trait::return_type, void>);
  static_assert(std::is_same_v<trait::first_argument_type, int>);
  static_assert(std::is_same_v<trait::last_argument_type, double>);
}

struct Dummy {
  int method(double, char) const noexcept { return 0; }
};

TEST(function_traitsTest, member_function_pointer)
{
  using trait = function_trait<&Dummy::method>;
  static_assert(trait::arity == 2);
  static_assert(std::is_same_v<trait::return_type, int>);
  static_assert(std::is_same_v<trait::first_argument_type, double>);
  static_assert(std::is_same_v<trait::last_argument_type, char>);
  static_assert(std::is_same_v<trait::class_type, const Dummy>);
}

TEST(function_traitsTest, type_based_trait)
{
  using trait = function_trait_t<int(double, char)>;
  static_assert(trait::arity == 2);
  static_assert(std::is_same_v<trait::return_type, int>);
  static_assert(std::is_same_v<trait::first_argument_type, double>);
  static_assert(std::is_same_v<trait::last_argument_type, char>);
}

TEST(function_traitsTest, lambda_trait)
{
  auto lambda = [](int x, double y) -> bool { return x > y; };
  using trait = function_trait_t<decltype(lambda)>;
  static_assert(trait::arity == 2);
  static_assert(std::is_same_v<trait::return_type, bool>);
  static_assert(std::is_same_v<trait::first_argument_type, int>);
  static_assert(std::is_same_v<trait::last_argument_type, double>);
}

} // namespace gh4ck3r::metatype
