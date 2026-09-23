#pragma once
#include <string_view>
#include <vector>

namespace gh4ck3r {

template <char SEP, char ESC = 0x00>
[[nodiscard]]
auto split(const std::string_view str)
{
  static_assert(!SEP || SEP != ESC, "SEP and ESC cannot be equal non-zero characters");

  std::vector<std::string_view> ret {};

  constexpr auto npos = std::string_view::npos;
  size_t beg = 0, end = 0;
  do {
    end = str.find_first_of(SEP, beg);
    if constexpr (ESC != 0x00) {
      while (end != npos) {
        if (auto i = end; i && str[--i] == ESC) [[likely]] {
          bool escaped = true;
          while (i-- > 0 && str[i] == ESC) escaped ^= true;
          if (escaped) {
            end = str.find_first_of(SEP, end + 1);
            continue;
          }
        }
        break;
      }
    }
    ret.emplace_back(str.substr(beg, end == npos ? npos : end - beg));
    beg = end + 1;
  } while (end != npos);

  return ret;
}

} // namespace gh4ck3r
