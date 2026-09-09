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

constexpr void list_add_tail (::list_head &new_node, ::list_head &head) {
  new_node.next = &head;
  new_node.prev = head.prev;
  head.prev->next = &new_node;
  head.prev = &new_node;
}

template <typename T>
struct member_type_extractor;

template <typename Class, typename Member>
struct member_type_extractor<Member Class::*> { using type = Member; };

template <typename T>
using member_type_extractor_t = member_type_extractor<T>::type;

template <auto Member, auto...NestedMembers>
struct list_node_binder {
  using members_t = std::tuple<decltype(Member), decltype(NestedMembers)...>;

  using first_mp_t = decltype(Member);
  using node_type = container_of_t<first_mp_t>;
  using last_mp_t = std::tuple_element_t<std::tuple_size_v<members_t> - 1, members_t>;

  static_assert(std::is_same_v<::list_head, member_type_extractor_t<last_mp_t>>);
};

template <auto...NestedMembers>
struct list_node_t {
  using binder_t = list_node_binder<NestedMembers...>;

  binder_t::node_type &node_;
};

template <auto...NestedMembers>
::list_head &operator<<(::list_head &head,
                        list_node_t<NestedMembers...> node)
{
  if (!head.prev) [[unlikely]] head.prev = &head;
  if (!head.next) [[unlikely]] head.next = &head;

  list_head *pn = reinterpret_cast<list_head*>(
    reinterpret_cast<unsigned char*>(&node.node_) + (detail::offset_of(NestedMembers) + ...));
  detail::list_add_tail(*pn, head);

  return head;
}

template <typename C, auto...NestedMembers>
struct list_node_container_t {
  using binder_t = list_node_binder<NestedMembers...>;
  static_assert(std::is_same_v<typename C::value_type, typename binder_t::node_type>);

  C &container_;
};

template <typename C, auto...NestedMembers>
::list_head &operator<<(::list_head &head,
                        list_node_container_t<C, NestedMembers...> container)
{
  for (auto &n : container.container_) head << list_node_t<NestedMembers...>(n);
  return head;
}

} // namespace detail

template <auto...NestedMembers>
constexpr auto list_node(typename detail::list_node_binder<NestedMembers...>::node_type &n) {
  return detail::list_node_t<NestedMembers...>{n};
}

template <typename N, ::list_head N::*M = &N::list>
constexpr auto list_node(N &n) { return list_node<M>(n); }

template <auto...NestedMembers, typename C,
          typename = std::enable_if_t<!!sizeof...(NestedMembers)>>
constexpr auto list_node_container(C &c) {
  return detail::list_node_container_t<C, NestedMembers...>{c};
}

template <typename C, ::list_head C::value_type::*M = &C::value_type::list>
constexpr auto list_node_container(C &c) {
  return detail::list_node_container_t<C, M>{c};
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

template <auto...NestedMembers>
class list_head_iterator {
  using binder_t = detail::list_node_binder<NestedMembers...>;
  list_head &head_;

 public:
  explicit list_head_iterator(list_head &head) : head_(head) {}

  class Iterator {
    list_head *p_;

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = binder_t::node_type;
    using difference_type   = std::ptrdiff_t;
    using pointer           = value_type*;
    using reference         = value_type&;

    Iterator() = default;
    explicit Iterator(list_head *p) : p_(p) {}
    inline reference operator*() const { return *operator->(); }

    inline pointer operator->() const {
      static const auto offset = (detail::offset_of(NestedMembers) + ...);

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
