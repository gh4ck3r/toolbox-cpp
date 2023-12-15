#pragma once
#include <utility>

namespace gh4ck3r {

template <class F, class...ETC>
constexpr auto recipe(F f, ETC&&...etc)
{
  if constexpr (sizeof...(ETC) == 0) return f;
  else return [=] (auto...args) {
    return recipe(etc...)(f(args...));
  };
}

} // namespace gh4ck3r
