#include "gh4ck3r/typemap.hh"
#include <gtest/gtest.h>

namespace gh4ck3r::metatype::typemap {

TEST(typemapTest, typedef_by_signed)
{
  static_assert(is_typedef_v<typedef_t<-1, std::string>>);
  static_assert(is_typedef_v<typedef_t<1, float>>);
}

TEST(typemapTest, typedef_by_unsigned)
{
  static_assert(is_typedef_v<typedef_t<1u, int>>);
  static_assert(is_typedef_v<typedef_t<3u, long>>);
}

TEST(typemapTest, typedef_by_enum)
{
  enum color { red, green, blue };

  static_assert(is_typedef_v<typedef_t<red, int>>);
  static_assert(is_typedef_v<typedef_t<green, float>>);
  static_assert(is_typedef_v<typedef_t<blue, std::string>>);
}

TEST(typemapTest, typedef_inherited)
{
  struct inherited_typedef1 : typedef_t<1, std::string> {
    static_assert(true, "add constraints like this!");
  };
  struct inherited_typedef2 : typedef_t<2, int> {
    static_assert(true, "add constraints like this!");
  };

  static_assert(is_typedef_v<inherited_typedef1>);
  static_assert(is_typedef_v<inherited_typedef2>);
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

} // namespace gh4ck3r::metatype::typemap
