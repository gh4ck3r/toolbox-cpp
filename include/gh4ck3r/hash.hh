#pragma once
#include <functional>
#include <cstddef>
#include <utility>
#include <type_traits>

namespace gh4ck3r {

namespace detail {

template <typename T>
struct is_pair : std::false_type {};

template <typename K, typename V>
struct is_pair<std::pair<K, V>> : std::true_type {};

template <typename T>
inline constexpr bool is_pair_v = is_pair<std::decay_t<T>>::value;

} // namespace detail

template <typename T>
inline constexpr std::size_t hash_value(const T &val);

template <typename K, typename V>
inline constexpr std::size_t hash_value(const std::pair<K, V> &p) {
  std::size_t seed = hash_value(p.first);
  constexpr std::size_t k = (sizeof(std::size_t) == 8)
      ? 0x9e3779b97f4a7c15ULL
      : 0x9e3779b9U;
  seed ^= (hash_value(p.second) + k + (seed << 6) + (seed >> 2));
  return seed;
}

template <typename T>
inline constexpr std::size_t hash_value(const T &val) {
  if constexpr (detail::is_pair_v<T>) {
    return hash_value(val);
  } else if constexpr (std::is_enum_v<T>) {
    using U = std::underlying_type_t<T>;
    return std::hash<U>{}(static_cast<U>(val));
  } else {
    return std::hash<T>{}(val);
  }
}

template <typename... ARGS>
inline constexpr std::size_t hash_combine(const ARGS&... args) {
  std::size_t seed = 0;
  constexpr std::size_t k = (sizeof(std::size_t) == 8)
      ? 0x9e3779b97f4a7c15ULL
      : 0x9e3779b9U;

  if constexpr (sizeof...(ARGS) > 0) {
    auto combine_one = [&seed, k](const auto &arg) {
      seed ^= (hash_value(arg) + k + (seed << 6) + (seed >> 2));
    };
    (combine_one(args), ...);
  }
  return seed;
}

template <typename C>
inline constexpr std::size_t unordered_hash_combine(const C &c) {
  std::size_t seed = 0;
  for (const auto &e : c) {
    seed ^= hash_value(e);
  }
  return seed;
}

} // namespace gh4ck3r
