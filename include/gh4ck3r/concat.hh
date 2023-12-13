#pragma once
#include <array>
#include <string_view>

namespace gh4ck3r {

template <char sep, const std::string_view&...S>
struct _concat_sv {
  constexpr static auto buf = [] {
    constexpr size_t total_siz = (S.size() + ...) + (sep ? sizeof...(S) : 0);
    std::array<char, total_siz + 1> buf {};
    auto append = [i = 0u, &buf] (auto const& s) mutable {
      if (sep) buf[i++] = sep;
      for (auto c : s) buf[i++] = c;
    };
    (append(S), ...);
    buf.back() = 0x00;  // null terminated
    return buf;
  }();
  constexpr static std::string_view value {buf.data(), buf.size() - 1};
};

template <const std::string_view&...Args>
inline constexpr std::string_view concat() {
  return _concat_sv<0x00, Args...>::value;
}

  template <class T, size_t M, size_t...MI, size_t N, size_t...NI>
inline constexpr std::array<T, M + N> concat(
    const std::array<T, M> &lhs,
    const std::array<T, N> &rhs,
    const std::index_sequence<MI...> &&,
    const std::index_sequence<NI...> &&)
{
  return {lhs[MI]..., rhs[NI]...};
}
template <class T>
inline constexpr std::array<T, 0> concat() { return {}; }
template <class T, size_t M>
inline constexpr auto concat(const std::array<T, M> &lhs) { return lhs; }
  template <class T, size_t M, size_t N, size_t...O>
inline constexpr auto concat(
    const std::array<T, M> &lhs,
    const std::array<T, N> &rhs,
    const std::array<T, O> &...etc)
{
  return concat( concat(
          lhs,
          rhs,
          std::make_index_sequence<M>{},
          std::make_index_sequence<N>{}
        ), etc...);
}

} // namespace gh4ck3r
