#pragma once
#include <tuple>
#include <cstddef>

namespace gh4ck3r::metatype {

namespace detail {

template <typename T>
struct remove_noexcept { using type = T; };

template <typename R, typename...ARGS>
struct remove_noexcept<R(ARGS...) noexcept> { using type = R(ARGS...); };

template <typename R, typename...ARGS>
struct remove_noexcept<R(*)(ARGS...) noexcept> : remove_noexcept<R(ARGS...)> {};

template <typename T>
using remove_noexcept_t = typename remove_noexcept<T>::type;

template <typename Fn>
struct function_trait;

template <typename R>
struct function_trait<R()> {
  using return_type = R;

  static inline constexpr size_t arity = 0;

  using first_argument_type = void;
  using last_argument_type = void;
};

template <typename R, typename...ARGS>
requires requires {sizeof...(ARGS);}
struct function_trait<R(ARGS...)> {
  using return_type = R;

  static inline constexpr size_t arity = sizeof...(ARGS);

  using args_tuple = std::tuple<ARGS...>;

  template <size_t N> requires requires {N < arity;}
  using arg_t = std::tuple_element_t<N, args_tuple>;

  using first_argument_type = arg_t<0>;
  using last_argument_type = arg_t<sizeof...(ARGS) - 1>;
};

template <typename R, typename...ARGS>
struct function_trait<R(*)(ARGS...)> : function_trait<R(ARGS...)> {};

} // namespace detail

template <auto Fn>
struct function_trait : detail::function_trait<
    detail::remove_noexcept_t<decltype(Fn)>>
{};

} // namespace gh4ck3r::metatype
