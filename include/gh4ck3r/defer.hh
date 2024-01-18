#pragma once
#include <functional>
#include <utility>

namespace gh4ck3r {

struct Defer : protected std::function<void()> {
    Defer() = delete;
    using base_t = function;

    template <class FN>
    explicit Defer(FN&& fn) : base_t(std::forward<FN>(fn)) {}
    ~Defer() { if (*this) base_t::operator()(); }

    using base_t::operator=;

    inline void release() { *this = nullptr; }
};

} // namespace gh4ck3r
