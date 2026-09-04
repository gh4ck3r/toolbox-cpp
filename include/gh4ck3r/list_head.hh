#pragma once
#include <iterator>
#include <tuple>
#include <type_traits>
#include <cstddef>

extern "C"
struct list_head {
  list_head *next, *prev;
};

namespace gh4ck3r::c_compat {

namespace detail {

template <typename T>
struct container_of;

template <typename T>
using container_of_t = typename container_of<T>::type;

template <typename ClassType, typename MemberType>
struct container_of<MemberType ClassType::*> {
  static_assert(std::is_standard_layout_v<ClassType>);
  using type = ClassType;
};

template <typename T, typename R>
static ptrdiff_t offset_of(R T::* Member) {
  return reinterpret_cast<ptrdiff_t>(&(reinterpret_cast<T*>(0)->*Member));
}

constexpr void list_add_tail (list_head &new_node, list_head &head) {
  new_node.next = &head;
  new_node.prev = head.prev;
  head.prev->next = &new_node;
  head.prev = &new_node;
}

} // namespace detail

template <typename C, auto Member>
struct list_node_binder { C &c_; };

template <typename C, list_head C::*Member = &C::list>
constexpr auto list_node(C &c) { return list_node_binder<C, Member>{c}; }

template <typename C, list_head C::value_type::*Member = &C::value_type::list>
constexpr auto list_node(C &c) { return list_node_binder<C, Member>{c}; }

template <auto Member, typename C>
constexpr auto list_node(C &c) { return list_node_binder<C, Member>{c}; }

template <typename C, auto Member>
list_head &operator<<(list_head &head, list_node_binder<C, Member> entry)
{
  if constexpr (!std::is_same_v<C, detail::container_of_t<decltype(Member)>>) {
    for (auto &e : entry.c_) {
      head << list_node<Member, typename C::value_type>(e);
    }
  } else {
    if (!head.prev) [[unlikely]] head.prev = &head;
    if (!head.next) [[unlikely]] head.next = &head;

    auto new_node = reinterpret_cast<list_head*>(
      reinterpret_cast<char*>(&entry.c_) + detail::offset_of(Member));
    detail::list_add_tail(*new_node, head);
  }
  return head;
}

namespace v1 {

template <typename T, ptrdiff_t OFFSET = offsetof(T, list)>
class list_head_iterator {
  list_head &head_;

 public:
  explicit list_head_iterator(list_head &head) : head_(head) {}

  class Iterator {
    list_head *p_;

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = T;
    using difference_type   = std::ptrdiff_t;
    using pointer           = T*;
    using reference         = T&;

    Iterator() = default;
    explicit Iterator(list_head *p) : p_(p) {}
    inline reference operator*() const { return *operator->(); }

    inline pointer operator->() const {
      return reinterpret_cast<pointer>(reinterpret_cast<char*>(p_) - OFFSET);
    }
    inline Iterator &operator++() { p_ = p_->next; return *this; }
    inline Iterator operator++(int) { auto tmp = *this; ++(*this); return tmp; }
    inline bool operator==(const Iterator &other) const { return p_ == other.p_; }
    // C++20 doesn't need following overload; compiler deduce it.
    inline bool operator!=(const Iterator &other) const { return p_ != other.p_; }
  };

  inline Iterator begin() { return Iterator {head_.next}; }
  inline Iterator end()   { return Iterator {&head_}; }
};

} // namespace v1

namespace v2 {

template <typename T>
struct member_type_extractor;

template <typename Class, typename Member>
struct member_type_extractor<Member Class::*> {
    using type = Member;
};

template <auto...NestingMembers>
class list_head_iterator {
  static_assert(sizeof...(NestingMembers));
  using MemberTypes = std::tuple<decltype(NestingMembers)...>;
  using first_t = std::tuple_element_t<0, MemberTypes>;
  using last_t = std::tuple_element_t<sizeof...(NestingMembers)-1, MemberTypes>;
  using last_member_type = typename member_type_extractor<last_t>::type;
  static_assert(std::is_same_v<list_head, last_member_type>);

  list_head &head_;

 public:
  explicit list_head_iterator(list_head &head) : head_(head) {}

  class Iterator {
    list_head *p_;

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = detail::container_of_t<first_t>;
    using difference_type   = std::ptrdiff_t;
    using pointer           = value_type*;
    using reference         = value_type&;

    Iterator() = default;
    explicit Iterator(list_head *p) : p_(p) {}
    inline reference operator*() const { return *operator->(); }

    inline pointer operator->() const {
      static const auto offset = (detail::offset_of(NestingMembers) + ...);

      return reinterpret_cast<pointer>(reinterpret_cast<char*>(p_) - offset);
    }
    inline Iterator &operator++() { p_ = p_->next; return *this; }
    inline Iterator operator++(int) { auto tmp = *this; ++(*this); return tmp; }
    inline bool operator==(const Iterator &other) const { return p_ == other.p_; }
    // C++20 doesn't need following overload; compiler deduce it.
    inline bool operator!=(const Iterator &other) const { return p_ != other.p_; }
  };

  inline Iterator begin() { return Iterator {head_.next}; }
  inline Iterator end()   { return Iterator {&head_}; }
};

} // namespace v2

inline namespace v3 {

template <typename T>
auto list_head_iterator(list_head &head) {
  return v2::list_head_iterator<&T::list>(head);
}

template <auto...Members>
auto list_head_iterator(list_head &head) {
  return v2::list_head_iterator<Members...>(head);
}

template <typename T, ptrdiff_t offset>
auto list_head_iterator(list_head &head) {
  return v1::list_head_iterator<T, offset>(head);
}

} // namespace v3

} // namespace gh4ck3r::c_compat
