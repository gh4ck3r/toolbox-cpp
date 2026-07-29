#pragma once
#include <functional>

namespace gh4ck3r {

template <typename...ARGS>
inline constexpr std::size_t hash_combine(const ARGS&...args) {
  std::size_t seed = 0;
  ((seed ^= std::hash<ARGS>{}(args)
          + 0x9e3779b9 + (seed << 6) + (seed >> 2)),...);
  return seed;
}

} // namespace gh4ck3r
