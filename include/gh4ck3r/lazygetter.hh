#pragma once
#include <functional>
#include <variant>
#include <mutex>
#include <type_traits>

namespace gh4ck3r {

template <typename GETTER,
         typename = std::enable_if_t<std::is_invocable_v<GETTER>>>
class LazyGetter {
  using T = std::invoke_result_t<GETTER>;
  static_assert(!std::is_const_v<T>);
  static_assert(!std::is_reference_v<T>);
  static_assert(!std::is_array_v<T>);
  static_assert(!std::is_void_v<T>);

  LazyGetter() = delete;

 public:
  explicit LazyGetter(GETTER getter) : val_(getter) {}

  operator T&() const {
    return std::visit([&] (auto &v) -> T& {
        using V = std::remove_reference_t<std::remove_cv_t<decltype(v)>>;
        if constexpr (std::is_same_v<V, GETTER>) {
          std::call_once(flag_, [&] {val_ = v();});
        }
        return std::get<T>(val_);
      }, val_);
  }

  template <typename U>
  inline T& operator=(U&& v) {
    static_assert(std::is_assignable_v<T&, U>);
    val_ = v;
    return *this;
  }

  inline bool operator==(const T rhs) const {
    return static_cast<T&>(*this) == rhs;
  }

 private:
  mutable std::variant<T, GETTER> val_;
  mutable std::once_flag flag_;

 private:
  friend inline bool operator==(const T lhs, const LazyGetter &rhs) {
    return rhs == lhs;
  }
};

}  // namespace gh4ck3r
