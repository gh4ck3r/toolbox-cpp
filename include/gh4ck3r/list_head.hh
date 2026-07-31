#pragma once
#include <iterator>
#include <type_traits>
#include <cstddef>

extern "C"
struct list_head {
  list_head* next;
  list_head* prev;
};

namespace gh4ck3r::c_compat {

namespace detail {

struct list_head : ::list_head {
  list_head() { next = prev = this; }
  list_head(list_head &other) : ::list_head(other) { prev->next = this; }
};

template <typename T>
struct container_of;

template <typename T>
using container_of_t = container_of<T>::type;

template <typename ClassType, typename MemberType>
struct container_of<MemberType ClassType::*> {
  static_assert(std::is_standard_layout_v<ClassType>);
  using type = ClassType;
};

template <auto Member, typename T = detail::container_of_t<decltype(Member)>>
static constexpr size_t offset() {
  return reinterpret_cast<size_t>(&(reinterpret_cast<T*>(0)->*Member));
}

} // namespace detail

template <auto Member, typename C>
detail::list_head make_list_head(C &c)
{
  constexpr auto add_tail = [] (list_head &head, list_head &new_node) {
    list_head* prev = head.prev;
    new_node.next = &head;
    new_node.prev = prev;
    prev->next = &new_node;
    head.prev = &new_node;
  };

  detail::list_head head {};
  for (auto &e : c) {
    add_tail(head, *reinterpret_cast<list_head*>(
      reinterpret_cast<char*>(&e) + detail::offset<Member>()));
  }

  return head;
}

namespace v1 {

template <typename T, size_t OFFSET = offsetof(T, list)>
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

    explicit Iterator(list_head *p) : p_(p) {}
    inline reference operator*() { return *operator->(); }

    inline pointer operator->() {
      return reinterpret_cast<pointer>(reinterpret_cast<char*>(p_) - OFFSET);
    }
    inline Iterator &operator++() { p_ = p_->next; return *this; }
    inline bool operator!=(const Iterator &other) { return p_ != other.p_; }
  };

  inline Iterator begin() { return Iterator {head_.next}; }
  inline Iterator end()   { return Iterator {&head_}; }
};

} // namespace v1

inline namespace v2 {
template <auto Member, typename T = detail::container_of_t<decltype(Member)>>
struct list_head_iterator {
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

    explicit Iterator(list_head *p) : p_(p) {}
    inline reference operator*() { return *operator->(); }

    inline pointer operator->() {
      return reinterpret_cast<pointer>(
        reinterpret_cast<char*>(p_) - detail::offset<Member, T>());
    }
    inline Iterator &operator++() { p_ = p_->next; return *this; }
    inline bool operator!=(const Iterator &other) { return p_ != other.p_; }
  };

  inline Iterator begin() { return Iterator {head_.next}; }
  inline Iterator end()   { return Iterator {&head_}; }
};
} // namespace v2

} // namespace gh4ck3r::list_head
