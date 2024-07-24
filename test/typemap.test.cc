#include "gh4ck3r/typemap.hh"
#include <gtest/gtest.h>

using gh4ck3r::typemap::typedef_t;
using gh4ck3r::typemap::declare_t;

TEST(typemapTest, typedef_by_signed)
{
  using gh4ck3r::typemap::is_typedef_v;

  static_assert(is_typedef_v<typedef_t<-1, std::string>>);
  static_assert(is_typedef_v<typedef_t<1, float>>);
}

TEST(typemapTest, typedef_by_unsigned)
{
  using gh4ck3r::typemap::is_typedef_v;

  static_assert(is_typedef_v<typedef_t<1u, int>>);
  static_assert(is_typedef_v<typedef_t<3u, long>>);
}

TEST(typemapTest, typedef_by_enum)
{
  enum color { red, green, blue };

  using gh4ck3r::typemap::is_typedef_v;
  static_assert(is_typedef_v<typedef_t<red, int>>);
  static_assert(is_typedef_v<typedef_t<green, float>>);
  static_assert(is_typedef_v<typedef_t<blue, std::string>>);

}

TEST(typemapTest, declare)
{
  using mytypes = declare_t<
      typedef_t<1u, int>,
      typedef_t<3u, long>,
      typedef_t<5u, std::string>
    >;

  static_assert(std::is_same_v<mytypes::at<1u>, int>);
  static_assert(std::is_same_v<mytypes::at<3u>, long>);
  static_assert(std::is_same_v<mytypes::at<5u>, std::string>);
}
