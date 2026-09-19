#pragma once
#include <array>
#include <string_view>
#include <utility>
#include <cstddef>

namespace gh4ck3r {

namespace detail {

template <char sep, const std::string_view&...S>
struct concat_sv {
  constexpr static auto buf = [] {
    constexpr size_t total_chars = (0u + ... + S.size());
    constexpr size_t num_seps = (sep != 0 && sizeof...(S) > 1) ? (sizeof...(S) - 1) : 0;
    constexpr size_t total_siz = total_chars + num_seps;

    std::array<char, total_siz + 1> buf {};
    size_t i = 0;
    bool first = true;
    auto append = [&](const auto& s) {
      if (!first && sep != 0) buf[i++] = sep;
      first = false;
      for (auto c : s) buf[i++] = c;
    };
    if constexpr (sizeof...(S) > 0) {
      (append(S), ...);
    } else {
      (void)append;
    }
    buf[total_siz] = '\0';
    return buf;
  }();
  constexpr static std::string_view value {buf.data(), buf.size() - 1};
};

template <class T, size_t M, size_t N, size_t... MI, size_t... NI>
inline constexpr std::array<T, M + N> concat_two(
    const std::array<T, M> &lhs,
    const std::array<T, N> &rhs,
    std::index_sequence<MI...>,
    std::index_sequence<NI...>)
{
  return {lhs[MI]..., rhs[NI]...};
}

} // namespace detail

template <const std::string_view&...Args>
inline constexpr std::string_view concat() {
  return detail::concat_sv<0x00, Args...>::value;
}

template <char sep, const std::string_view&...Args>
inline constexpr std::string_view concat_sep() {
  return detail::concat_sv<sep, Args...>::value;
}

template <class T>
inline constexpr std::array<T, 0> concat() { return {}; }

template <class T, size_t M>
inline constexpr auto concat(const std::array<T, M> &lhs) { return lhs; }

template <class T, size_t M, size_t N>
inline constexpr std::array<T, M + N> concat(
    const std::array<T, M> &lhs,
    const std::array<T, N> &rhs)
{
  return detail::concat_two(lhs, rhs,
                            std::make_index_sequence<M>{},
                            std::make_index_sequence<N>{});
}

template <class T, size_t M, size_t N, size_t... O>
inline constexpr auto concat(
    const std::array<T, M> &lhs,
    const std::array<T, N> &rhs,
    const std::array<T, O> &...etc)
{
  return concat(concat(lhs, rhs), etc...);
}

} // namespace gh4ck3r
