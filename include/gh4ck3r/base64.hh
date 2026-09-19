#pragma once
#include <iterator>
#include <string>
#include <string_view>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace gh4ck3r::base64 {

template<class It>
std::string encode(It beg, const It end)
{
  static_assert(1 == sizeof(*beg));
  if (beg == end) return {};

  constexpr std::string_view enc_tbl {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/"
  };

  std::string ret;
  if constexpr (std::is_base_of_v<std::random_access_iterator_tag,
    typename std::iterator_traits<It>::iterator_category>) {
    const auto len = static_cast<size_t>(std::distance(beg, end));
    ret.reserve(((len + 2) / 3) * 4);
  }

  uint32_t val = 0;
  int val_b = -6;

  for (; beg != end; ++beg) {
    val = (val << 8) | static_cast<uint8_t>(*beg);
    val_b += 8;
    while (val_b >= 0) {
      ret.push_back(enc_tbl[(val >> val_b) & 0x3F]);
      val_b -= 6;
    }
  }

  if (val_b > -6) ret.push_back(enc_tbl[(val << (-val_b)) & 0x3F]);

  while (ret.size() % 4 != 0) ret.push_back('=');

  return ret;
}

template<class T>
T decode_as(const std::string_view b64str)
{
  constexpr int8_t dec_tbl[256] {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 00-0F */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 10-1F */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, /* 20-2F */
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, /* 30-3F */
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, /* 40-4F */
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, /* 50-5F */
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, /* 60-6F */
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, /* 70-7F */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 80-8F */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 90-9F */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* A0-AF */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* B0-BF */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* C0-CF */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* D0-DF */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* E0-EF */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  /* F0-FF */
  };

  T ret;
  if constexpr (requires { ret.reserve(size_t{}); }) {
    ret.reserve(b64str.size() * 3 / 4);
  }

  uint32_t val = 0;
  int val_b = -8;
  for (const auto &c : b64str) {
    if (c == '=') break;
    const auto idx = static_cast<uint8_t>(c);
    const auto d = dec_tbl[idx];
    if (d < 0) {
      throw std::invalid_argument {
        "trying to decode invalid base64 string : " + std::string{b64str}
      };
    }
    val = (val << 6) | static_cast<uint32_t>(d);
    val_b += 6;
    if (val_b >= 0) {
      ret.insert(ret.end(), static_cast<typename T::value_type>((val >> val_b) & 0xFF));
      val_b -= 8;
    }
  }
  return ret;
}

template <class T, class = std::enable_if_t<std::is_object_v<T>>>
inline std::string encode(T&& data) {
  return encode(begin(std::forward<T>(data)), end(std::forward<T>(data)));
}

template <size_t N>
inline std::string encode(const char (&lit)[N]) {
  static_assert(N > 0);
  const size_t len = (N > 0 && lit[N - 1] == '\0') ? N - 1 : N;
  return encode(lit, lit + len);
}

inline std::string decode(const std::string_view base64_str) {
  return decode_as<std::string>(base64_str);
}

} // namespace gh4ck3r::base64
