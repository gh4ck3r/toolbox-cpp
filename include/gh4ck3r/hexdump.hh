#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <ios>
#include <iterator>
#include <memory>
#include <sstream>
#include <type_traits>

namespace gh4ck3r {

namespace detail {

template <typename T>
constexpr auto to_unsigned_hex_val(const T& c) {
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
    if constexpr (sizeof(T) <= 2) {
      return static_cast<unsigned int>(static_cast<std::make_unsigned_t<T>>(c));
    } else if constexpr (sizeof(T) == 4) {
      return static_cast<uint32_t>(c);
    } else {
      return static_cast<uint64_t>(c);
    }
  } else if constexpr (std::is_same_v<T, std::byte>) {
    return static_cast<unsigned int>(static_cast<uint8_t>(c));
  } else {
    if constexpr (sizeof(T) <= 4) {
      uint32_t u{0};
      std::memcpy(&u, std::addressof(c), sizeof(T));
      return u;
    } else {
      uint64_t u{0};
      std::memcpy(&u, std::addressof(c), std::min(sizeof(T), sizeof(uint64_t)));
      return u;
    }
  }
}

} // namespace detail

template <typename Iter>
auto hexdump(const Iter beg, const Iter end)
{
  std::ostringstream oss;
  oss << std::hex;

  constexpr auto col_bytes = sizeof(*beg);
  constexpr std::ptrdiff_t width = 0x10 / col_bytes;
  static_assert(width > 0, "col_bytes must be <= 16");

  using hex_type = decltype(detail::to_unsigned_hex_val(*beg));

  for (auto cur = beg; cur != end; oss << '\n')
  {
    const auto remaining = std::distance(cur, end);
    const auto ncols = std::min(width, remaining);
    const auto cur_end = std::next(cur, ncols);

    oss << std::setfill('0')
      << reinterpret_cast<const void*>(std::addressof(*cur))
      << "  ";

    std::transform(cur, cur_end, std::ostream_iterator<hex_type> {oss, " "},
        [&oss] (const auto &c) {
          oss.width(2 * col_bytes);
          return detail::to_unsigned_hex_val(c);
        });

    std::fill_n(std::ostream_iterator<char> {oss}, (1 + 2 * col_bytes) * (width - ncols), ' ');
    std::transform(cur, cur_end, std::ostream_iterator<char> {oss << ' '},
        [] (const auto &c) {
          const auto u = detail::to_unsigned_hex_val(c);
          return (u >= 0x20 && u <= 0x7e) ? static_cast<char>(u) : '.';
        });

    cur = cur_end;
  }

  return oss.str();
}

template <typename T>
inline decltype(auto) hexdump(const T& arg) {
  return hexdump(std::begin(arg), std::end(arg));
}

inline decltype(auto) hexdump(const void *ptr, const size_t len) {
  const auto p = reinterpret_cast<const uint8_t *>(ptr);
  return hexdump(p, p + len);
}

} // namespace gh4ck3r
