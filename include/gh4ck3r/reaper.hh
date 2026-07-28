#pragma once
#include <utility>
#include <system_error>
#include <unistd.h>
#include "function_traits.hh"
#include "type_traits.hh"

namespace gh4ck3r::reaper {

namespace detail {

using metatype::function_trait;

template <auto Deleter>
requires (function_trait<Deleter>::arity == 1)
using resource_t = function_trait<Deleter>::first_argument_type;

template <auto Deleter>
constexpr resource_t<Deleter> invalid_value()
{
  if constexpr (std::is_same_v<int, resource_t<Deleter>>)
    return -1;

  return {};
}

} // namespace detail

template <auto Deleter,
          detail::resource_t<Deleter> INVALID = detail::invalid_value<Deleter>()>
class Reaper {
  using resource_type = detail::resource_t<Deleter>;
  resource_type resource_;

  inline void reset(resource_type r = INVALID) {
    if (resource_ != INVALID) Deleter(std::exchange(resource_, r));
  }
 public:
  Reaper(resource_type &&r) : resource_ (std::exchange(r, INVALID)) {
    if (resource_ == INVALID) [[unlikely]] {
      if (errno) throw std::system_error {errno, std::system_category(),
          "Reaper:failed to acquire resource"};

      throw std::invalid_argument {"Reaper:invalid resource"};
    }
  }

  inline Reaper &operator=(resource_type &&r) {
    if (r == INVALID)
      throw std::invalid_argument {"Reaper: assigning invalid resource"};

    reset(std::exchange(r, INVALID));

    return *this;
  }

  ~Reaper() { reset(); }

  [[nodiscard]]
  inline resource_type release() {
    return std::exchange(resource_, INVALID);
  }

  inline resource_type operator->() const
  requires (std::is_pointer_v<resource_type> && metatype::is_complete_v<std::remove_pointer_t<resource_type>> ) {
    return resource_; }
  operator bool () const { return resource_ != INVALID; }

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

Reaper(FILE*) -> Reaper<std::fclose>;

} // namespace gh4ck3r::reaper
