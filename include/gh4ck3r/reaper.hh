#pragma once
#include <utility>
#include <system_error>
#include <unistd.h>
#include "function_traits.hh"
#include "type_traits.hh"

namespace gh4ck3r {

using metatype::function_trait;

template <auto Deleter>
requires (function_trait<Deleter>::arity == 1)
struct resource_of {
  using type = function_trait<Deleter>::first_argument_type;
  static inline constexpr type invalid_value() { return {}; }
};

template <auto Deleter>
requires (function_trait<Deleter>::arity == 1)
class Reaper {
  using resource_type = resource_of<Deleter>::type;
  static inline constexpr auto invalid_ = resource_of<Deleter>::invalid_value();
  resource_type resource_;

  inline void reset(resource_type r = invalid_) {
    if (resource_ != invalid_) Deleter(std::exchange(resource_, r));
  }
 public:
  Reaper(resource_type &&r) : resource_ (std::exchange(r, invalid_)) {
    if (resource_ == invalid_) [[unlikely]] {
      if (errno) throw std::system_error {errno, std::system_category(),
          "Reaper:failed to acquire resource"};

      throw std::invalid_argument {"Reaper:invalid resource"};
    }
  }

  inline Reaper &operator=(resource_type &&r) {
    if (r == invalid_)
      throw std::invalid_argument {"Reaper: assigning invalid resource"};

    reset(std::exchange(r, invalid_));

    return *this;
  }

  ~Reaper() { reset(); }

  [[nodiscard]]
  inline resource_type release() {
    return std::exchange(resource_, invalid_);
  }

  inline resource_type operator->() const
  requires (std::is_pointer_v<resource_type> && metatype::is_complete_v<std::remove_pointer_t<resource_type>> ) {
    return resource_; }
  operator bool () const { return resource_ != invalid_; }

  inline operator resource_type () const { return resource_; }

  template <typename P>
  requires (std::is_same_v<resource_type, void*> && std::is_pointer_v<P>)
  operator P () const { return reinterpret_cast<P>(resource_); }

 private:
  Reaper() = delete;
  Reaper(const Reaper &) = delete;
  Reaper(const resource_type &) = delete;

  Reaper &operator=(const Reaper &) = delete;
  Reaper &operator=(const resource_type &) = delete;
};

// XXX: I don't like this
template <>
inline constexpr int resource_of<::close>::invalid_value() { return -1; }

Reaper(FILE*) -> Reaper<std::fclose>;

} // namespace gh4ck3r
