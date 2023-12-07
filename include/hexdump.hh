#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <iterator>
#include <memory>
#include <sstream>
#include <tuple>

namespace gh4ck3r {

template <typename Iter>
auto hexdump(const Iter beg, const Iter end)
{
  std::ostringstream oss;
  oss << std::hex;

  constexpr auto col_bytes = sizeof(*beg);
  constexpr std::ptrdiff_t width = 0x10 / col_bytes;
  static_assert(width);
  for (auto [cur, ncols] = std::tuple {beg, std::ptrdiff_t{}};
      cur != end;
      cur += ncols, oss << '\n')
  {
    ncols = std::min(width , end - cur);

    oss << std::setfill('0')
      << reinterpret_cast<const void*>(std::addressof(*beg) + (cur - beg))
      << "  ";

    transform(cur, cur + ncols, std::ostream_iterator<int> {oss, " "},
        [&oss] (const auto &c) {
          oss.width(2 * col_bytes);
          return 0xff & c;
        });
    std::fill_n(std::ostream_iterator<char> {oss}, (1 + 2 * col_bytes) * (width - ncols), ' ');
    transform(cur, cur + ncols, std::ostream_iterator<char> {oss << ' '},
        [] (const auto &c) { return std::isprint(c) ? c : '.'; });
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
