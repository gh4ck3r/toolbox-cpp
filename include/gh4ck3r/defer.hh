#pragma once
#include <functional>
#include <utility>
#include <stdexcept>
#include <type_traits>

namespace gh4ck3r {

template <typename F = std::function<void()>>
class Defer {
 public:
  Defer() = delete;

  template <typename FN>
  requires (!std::is_same_v<std::decay_t<FN>, Defer>)
  explicit Defer(FN&& fn) : f_(std::forward<FN>(fn)), active_(true) {}

  ~Defer() noexcept {
    if (active_) {
      try {
        f_();
      } catch (...) {}
    }
  }

  // Disable copy operations to prevent double execution bugs
  Defer(const Defer&) = delete;
  Defer& operator=(const Defer&) = delete;

  // Move constructor
  Defer(Defer&& other) noexcept(std::is_nothrow_move_constructible_v<F>)
      : f_(std::move(other.f_)), active_(std::exchange(other.active_, false)) {}

  // Disable move assignment
  Defer& operator=(Defer&&) = delete;

  // Nullptr assignment to disarm/release
  Defer& operator=(std::nullptr_t) noexcept {
    release();
    return *this;
  }

  inline void release() noexcept {
    active_ = false;
  }

  inline void operator()() {
    if (!active_) [[unlikely]] throw std::logic_error {"Defer object is empty"};
    active_ = false;
    f_();
  }

  explicit operator bool() const noexcept {
    return active_;
  }

 private:
  F f_;
  bool active_{true};
};

// CTAD deduction guide
template <typename F>
Defer(F) -> Defer<F>;

} // namespace gh4ck3r
