#pragma once
#include <tuple>
#include <cstddef>
#include <type_traits>

namespace gh4ck3r::metatype {

namespace detail {

template <typename R, typename... ARGS>
struct function_traits_base {
  using return_type = R;
  static inline constexpr size_t arity = sizeof...(ARGS);
  using args_tuple = std::tuple<ARGS...>;

  template <size_t N> requires (N < arity)
  using arg_t = std::tuple_element_t<N, args_tuple>;

  using first_argument_type = std::conditional_t<(arity > 0), std::tuple_element_t<0, std::tuple<ARGS..., void>>, void>;
  using last_argument_type  = std::conditional_t<(arity > 0), std::tuple_element_t<(arity > 0 ? arity - 1 : 0), std::tuple<ARGS..., void>>, void>;
};

template <typename T>
struct function_traits_impl;

// 1. Plain functions
template <typename R, typename... ARGS>
struct function_traits_impl<R(ARGS...)> : function_traits_base<R, ARGS...> {};

template <typename R, typename... ARGS>
struct function_traits_impl<R(ARGS...) noexcept> : function_traits_base<R, ARGS...> {};

// 2. Function pointers
template <typename R, typename... ARGS>
struct function_traits_impl<R(*)(ARGS...)> : function_traits_base<R, ARGS...> {};

template <typename R, typename... ARGS>
struct function_traits_impl<R(*)(ARGS...) noexcept> : function_traits_base<R, ARGS...> {};

// 3. Member function pointers
template <typename C, typename R, typename... ARGS>
struct function_traits_impl<R(C::*)(ARGS...)> : function_traits_base<R, ARGS...> {
  using class_type = C;
};

template <typename C, typename R, typename... ARGS>
struct function_traits_impl<R(C::*)(ARGS...) noexcept> : function_traits_base<R, ARGS...> {
  using class_type = C;
};

template <typename C, typename R, typename... ARGS>
struct function_traits_impl<R(C::*)(ARGS...) const> : function_traits_base<R, ARGS...> {
  using class_type = const C;
};

template <typename C, typename R, typename... ARGS>
struct function_traits_impl<R(C::*)(ARGS...) const noexcept> : function_traits_base<R, ARGS...> {
  using class_type = const C;
};

template <typename C, typename R, typename... ARGS>
struct function_traits_impl<R(C::*)(ARGS...) &> : function_traits_base<R, ARGS...> {
  using class_type = C;
};

template <typename C, typename R, typename... ARGS>
struct function_traits_impl<R(C::*)(ARGS...) & noexcept> : function_traits_base<R, ARGS...> {
  using class_type = C;
};

template <typename C, typename R, typename... ARGS>
struct function_traits_impl<R(C::*)(ARGS...) const &> : function_traits_base<R, ARGS...> {
  using class_type = const C;
};

template <typename C, typename R, typename... ARGS>
struct function_traits_impl<R(C::*)(ARGS...) const & noexcept> : function_traits_base<R, ARGS...> {
  using class_type = const C;
};

// 4. Functors / Lambdas
template <typename T>
requires requires { &T::operator(); }
struct function_traits_impl<T> : function_traits_impl<decltype(&T::operator())> {};

} // namespace detail

// Type-based function_traits
template <typename T>
struct function_trait_t : detail::function_traits_impl<std::remove_cvref_t<T>> {};

// Value/NTTP-based function_traits
template <auto Fn>
struct function_trait : detail::function_traits_impl<std::remove_cvref_t<decltype(Fn)>> {};

} // namespace gh4ck3r::metatype
