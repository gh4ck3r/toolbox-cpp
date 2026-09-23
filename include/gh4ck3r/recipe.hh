#pragma once
#include <functional>
#include <tuple>
#include <utility>

namespace gh4ck3r {

template <typename F, typename... ETC>
constexpr auto recipe(F&& f, ETC&&... etc) {
  if constexpr (sizeof...(ETC) == 0) {
    return std::forward<F>(f);
  } else {
    return [f = std::forward<F>(f), etc = std::make_tuple(std::forward<ETC>(etc)...)] (auto&&... args) {
      return std::apply([&](const auto&... rest_fns) {
        return recipe(rest_fns...)(std::invoke(f, std::forward<decltype(args)>(args)...));
      }, etc);
    };
  }
}

} // namespace gh4ck3r
