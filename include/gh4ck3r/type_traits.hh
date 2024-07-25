#pragma once
#include <type_traits>

namespace gh4ck3r::metatype {

// check if given type is defined or not.
template<typename, typename = void> struct is_complete : std::false_type {};
template<typename T> struct is_complete<T, std::void_t<decltype(sizeof(T))>> :
    std::true_type {};

template<typename T>
constexpr bool is_complete_v = is_complete<T>::value;

} // namespace gh4ck3r::metatype
