#pragma once
#include <string_view>
#include <vector>

namespace gh4ck3r {

template <char SEP>
auto split(const std::string_view str)
{
  std::vector<std::string_view> ret {};

  constexpr auto npos = std::string_view::npos;
  size_t beg = 0, end = 0;
  do {
    end = str.find_first_of(SEP, beg);
    ret.emplace_back(str.substr(beg, end == npos ? npos : end - beg));
    beg = end + 1;
  } while (end != npos);

  return ret;
}


} // namespace gh4ck3r
