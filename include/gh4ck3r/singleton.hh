#pragma once
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace gh4ck3r {
namespace singleton {
template <typename T>
inline T *default_ctor() { return new T{}; }

template <typename T>
inline void default_dtor(T *p) { delete p; };

template<typename, typename = void>
constexpr bool is_type_complete_v = false;

template<typename T>
constexpr bool is_type_complete_v<T, std::void_t<decltype(typeid(T))>> = true;

template <typename T,
         T *(*create_instance)() = default_ctor<T>,
         void (*destroy_instance)(T*) = default_dtor<T>>
class SharedSingleton : public std::shared_ptr<T> {
  using shared_ptr = std::shared_ptr<T>;

  static inline typename shared_ptr::weak_type weak_instance;
  static inline auto get_instance() {
    static std::mutex m;
    std::lock_guard lk{m};
    auto sp = weak_instance.lock();
    if (!sp) {
      if (T* p = create_instance(); p) {
        sp.reset(p, destroy_instance);
        weak_instance = sp;
      } else {
        std::ostringstream oss;
        oss << "failed to create SharedSingleton instance";
        if constexpr (std::is_same_v<void, T>
            || std::is_scalar_v<T>
            || is_type_complete_v<T>)
        {
          oss << " for " << typeid(T).name();
        }
        throw std::runtime_error {oss.str()};
      }
    }
    return sp;
  }

 public:
  SharedSingleton() : shared_ptr(get_instance()) {}
  inline operator auto() const { return shared_ptr::get(); }

  static inline auto use_count() { return weak_instance.use_count(); }
};
} // namespace singleton

using singleton::SharedSingleton;

} // namespace gh4ck3r
