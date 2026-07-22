#pragma once
#include <functional>
#include <utility>
#include <stdexcept>

namespace gh4ck3r {

struct Defer : protected std::function<void()> {
  Defer() = delete;
  using base_t = function;

  template <class FN>
  explicit Defer(FN&& fn) : base_t(std::forward<FN>(fn)) {}
  ~Defer() noexcept { if (*this) try {base_t::operator()();} catch(...){} }

  using base_t::operator=;

  inline void release() { *this = nullptr; }
  inline void operator()() {
    if (!*this) [[unlikely]] throw std::logic_error {"Defer object is empty"};

    base_t f{};
    swap(f);

    f();
  }
};

} // namespace gh4ck3r
