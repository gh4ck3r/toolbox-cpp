#pragma once
#include <string_view>
#include <vector>

namespace gh4ck3r {

template <char SEP, char ESC = 0x00>
auto split(const std::string_view str)
{
  static_assert(!SEP || SEP != ESC);

  std::vector<std::string_view> ret {};

  constexpr auto npos = std::string_view::npos;
  size_t beg = 0, end = 0;
  do {
    end = str.find_first_of(SEP, beg);
    if constexpr (ESC) {
      if (auto i = end; i && i != npos && str[--i] == ESC) [[likely]] {
        bool escaped = true;
        while (i-->0 && str[i] == ESC) escaped ^= true;
        if (escaped) end = str.find_first_of(SEP, end + 1);
      }
    }
    ret.emplace_back(str.substr(beg, end == npos ? npos : end - beg));
    beg = end + 1;
  } while (end != npos);

  return ret;
}


} // namespace gh4ck3r
