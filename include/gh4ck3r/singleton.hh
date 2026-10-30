#pragma once
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <cassert>

namespace gh4ck3r {
namespace singleton {

namespace detail {

template <typename T>
constexpr std::string_view debug_type_name() {
  std::string_view name = __PRETTY_FUNCTION__;
  size_t prefix_len = 
#if defined(__clang__)
    name.find("[T = ") + 5;
#elif defined(__GNUC__)
    name.find("[with T = ") + 10;
#else
  return "UnknownCompiler";
#endif
  size_t suffix_len = name.length() - prefix_len - 1;
  name = name.substr(prefix_len, suffix_len);
  return name.substr(0, name.find(";"));
}

} // namespace detail

template <typename T>
inline T *default_ctor() { return new T{}; }

template <typename T>
inline void default_dtor(T *p) { delete p; }

template <typename T,
         T *(*create_instance)() = default_ctor<T>,
         void (*destroy_instance)(T*) = default_dtor<T>>
class SharedSingleton : private std::shared_ptr<T> {
  using shared_ptr = std::shared_ptr<T>;

  static inline typename shared_ptr::weak_type weak_instance;
  static inline std::mutex instance_mutex;

  static inline auto get_instance() {
    std::lock_guard lk{instance_mutex};
    auto sp = weak_instance.lock();
    if (!sp) {
      if (T* p = create_instance(); p) {
        sp.reset(p, destroy_instance);
        weak_instance = sp;
      } else {
        std::ostringstream oss;
        oss << "failed to create SharedSingleton instance for "
            << detail::debug_type_name<T>();
        throw std::runtime_error {oss.str()};
      }
    }
    return sp;
  }

 public:
  SharedSingleton() : shared_ptr(get_instance()) {}
  inline operator T*() const { return shared_ptr::get(); }

  using shared_ptr::operator->;
  using shared_ptr::get;

  static inline auto use_count() {
    std::lock_guard lk{instance_mutex};
    return weak_instance.use_count();
  }
};

class SingletonTraits {
 protected:
  SingletonTraits()  = default;
  ~SingletonTraits() = default;
  SingletonTraits(const SingletonTraits&)            = delete;
  SingletonTraits& operator=(const SingletonTraits&) = delete;
};

template <typename T>
class StaticSingleton final : public T, private SingletonTraits {
  StaticSingleton() = default;
 public:
  static inline T &instance() {
    static StaticSingleton s {};
    return s;
  }
};

template <typename T>
class ThreadSingleton {
  static inline thread_local bool created_ = false;
  std::thread::id owner_ = std::this_thread::get_id();
 protected:
  ThreadSingleton() {
    if (created_) throw std::logic_error {
      std::string{"ThreadSingleton: "} + typeid(T).name() + " exists already"
    };
    created_ = true;
  }
  ~ThreadSingleton() noexcept {
    assert(std::this_thread::get_id() == owner_);
    created_ = false;
  }
  ThreadSingleton(const ThreadSingleton&) = delete;
  ThreadSingleton &operator=(const ThreadSingleton&) = delete;
  ThreadSingleton(ThreadSingleton&&) = delete;
  ThreadSingleton &operator=(ThreadSingleton&&) = delete;

  const std::thread::id &owner() const { return owner_; }
};
} // namespace singleton

using singleton::SharedSingleton;
using singleton::StaticSingleton;
using singleton::ThreadSingleton;

} // namespace gh4ck3r
