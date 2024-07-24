#pragma once
#include <tuple>
#include <type_traits>

namespace gh4ck3r::typemap {

template <auto KEY>
using key_t = std::integral_constant<decltype(KEY), KEY>;

template <auto KEY, typename T>
struct typedef_t {
  static inline constexpr auto key = KEY;
  using mapped_type = T;

  static typedef_t typefor(key_t<KEY>);
};

template <typename VALUE>
constexpr bool is_typedef_v = std::is_base_of_v<
    typedef_t<VALUE::key, typename VALUE::mapped_type>,
    VALUE>;

template <typename...TDEFS>
struct declare_t : TDEFS... {
  static_assert((is_typedef_v<TDEFS> && ...),
      "should be defined with typedef_t");

  using TDEFS::typefor...;
  using first_t = std::tuple_element_t<0, std::tuple<TDEFS...>>;

  template <decltype(first_t::key) KEY>
  using at = typename decltype(typefor(key_t<KEY>{}))::mapped_type;
};

}  // namespace gh4ck3r::typemap
