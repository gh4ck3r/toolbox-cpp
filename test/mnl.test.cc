#include <array>
#include <format>
#include <random>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <libmnl/libmnl.h>
#include <gtest/gtest.h>

void print(const nlmsghdr * const hdr)
{
	std::clog << "nlmsg_len  : " << hdr->nlmsg_len << std::endl;
  std::clog << "nlmsg_type : " << hdr->nlmsg_type << std::endl;
  std::clog << "nlmsg_flags: " << hdr->nlmsg_flags << std::endl;
  std::clog << "nlmsg_seq  : " << hdr->nlmsg_seq << std::endl;
  std::clog << "nlmsg_pid  : " << hdr->nlmsg_pid << std::endl;
}

namespace netlink {

enum class Family {
  ROUTE           = NETLINK_ROUTE,
  UNUSED          = NETLINK_UNUSED,
  USERSOCK        = NETLINK_USERSOCK,
  FIREWALL        = NETLINK_FIREWALL,
  SOCK_DIAG       = NETLINK_SOCK_DIAG,
  NFLOG           = NETLINK_NFLOG,
  XFRM            = NETLINK_XFRM,
  SELINUX         = NETLINK_SELINUX,
  ISCSI           = NETLINK_ISCSI,
  AUDIT           = NETLINK_AUDIT,
  FIB_LOOKUP      = NETLINK_FIB_LOOKUP,
  CONNECTOR       = NETLINK_CONNECTOR,
  NETFILTER       = NETLINK_NETFILTER,
  IP6_FW          = NETLINK_IP6_FW,
  DNRTMSG         = NETLINK_DNRTMSG,
  KOBJECT_UEVENT  = NETLINK_KOBJECT_UEVENT,
  GENERIC         = NETLINK_GENERIC,
  SCSITRANSPORT   = NETLINK_SCSITRANSPORT,
  ECRYPTFS        = NETLINK_ECRYPTFS,
  RDMA            = NETLINK_RDMA,
  CRYPTO          = NETLINK_CRYPTO,
  SMC             = NETLINK_SMC,
};


template <typename F, typename EXTHDR>
concept header_initiator_t = std::is_invocable_v<F, nlmsghdr&, EXTHDR&>;
  using buf_t = std::vector<uint8_t>;

template <Family BUS>
class Socket {
 public:
  Socket(const int flags = 0) :
    socket_(mnl_socket_open2(static_cast<int>(BUS), flags)),
    seq_(std::mt19937{std::random_device{}()}())
  {
    if (!socket_) [[unlikely]] throw std::system_error {
      errno,
      std::system_category(),
      "Failed to create netlink socket"
    };
  }
  Socket(const Socket &) = delete;
  Socket(Socket &&) = delete;
  Socket &operator=(const Socket &) = delete;
  Socket &operator=(Socket &&) = delete;

  ~Socket() noexcept { mnl_socket_close(socket_); }

  inline auto portid() const { return mnl_socket_get_portid(socket_); }

  inline void bind(const unsigned int groups, const pid_t pid = MNL_SOCKET_AUTOPID) {
    if (mnl_socket_bind(socket_, groups, pid) == -1)
      [[unlikely]] throw std::system_error{ errno, std::system_category(),
        "failed to bind netlink socket"
      };
  }

 protected:
  const auto socket() const { return socket_; }
  using msgid_t = uint32_t;

  template <typename EXTHDR, header_initiator_t<EXTHDR> CALLBACK>
  msgid_t send(CALLBACK callback) {
    buf_t buf(MNL_SOCKET_BUFFER_SIZE, 0);

    auto const nlh = mnl_nlmsg_put_header(buf.data());
    callback(*nlh, *reinterpret_cast<std::add_pointer_t<EXTHDR>>(
      mnl_nlmsg_put_extra_header(nlh, sizeof(EXTHDR))));

    nlh->nlmsg_seq = seq_++;

    for (ssize_t nsent = 0, nleft = nlh->nlmsg_len; nleft; nleft -= nsent) {
      nsent = mnl_socket_sendto(socket_, nlh + nsent, nleft);
      if (nsent == -1) [[unlikely]] throw std::system_error {
        errno,
        std::system_category(),
        "failed to send netlink msg"};
    }
    return nlh->nlmsg_seq;
  }

  auto recv() {
    buf_t buf (MNL_SOCKET_BUFFER_SIZE, 0);

    auto nrecv = mnl_socket_recvfrom(socket(), buf.data(), buf.size());
		if (nrecv == -1 && errno == ENOSPC) [[unlikely]] {
      buf.resize(MNL_SOCKET_DUMP_SIZE);
      nrecv = mnl_socket_recvfrom(
          socket(),
          &buf.at(MNL_SOCKET_BUFFER_SIZE),
          MNL_SOCKET_DUMP_SIZE - MNL_SOCKET_BUFFER_SIZE);
    }

    if (nrecv != -1) [[likely]] buf.resize(nrecv);
    else throw std::system_error { errno, std::system_category(),
        "failed to recvfrom netlink socket"};

    return buf;
  }

 private:
  mnl_socket * const socket_;
  msgid_t seq_;
};

using Netfilter = Socket<Family::NETFILTER>;
namespace netfilter {
namespace conntrack {

namespace tuple {

template <typename T>
concept tuple_enum_t = std::is_enum_v<T>;

template <tuple_enum_t E, E ATTR>
struct AttrTraits {
  using type = void;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_UNSPEC;
  static constexpr std::string_view name {"Unknown"};
};

template <tuple_enum_t E, size_t...I>
constexpr auto __attribute_types(std::index_sequence<I...>) {
  return std::array { AttrTraits<E, static_cast<E>(I)>::type_index... };
}

template <tuple_enum_t E, size_t...I>
constexpr auto __attribute_names(std::index_sequence<I...>) {
  return std::array { AttrTraits<E, static_cast<E>(I)>::name... };
}

template <tuple_enum_t E>
struct TupleTraits {
  static constexpr auto max_index = 0;
};


template <tuple_enum_t E>
class Attribute {
  static constexpr auto data_types {
    __attribute_types<E>(std::make_index_sequence<TupleTraits<E>::max_index>{})
  };

 public:
  Attribute(const nlattr & hdr) :
    id_(static_cast<E>(mnl_attr_get_type(&hdr))),
    type_(data_types[id_]),
    data_(mnl_attr_get_payload(&hdr))
  {
    mnl_attr_validate(&hdr, type_);  // TODO: check return value
  }

  template <typename T>
  inline auto value() const {
    if constexpr (std::is_same_v<std::decay_t<T>, void>) {
      return data_;
    }
    else if constexpr (std::is_same_v<std::decay_t<T>, std::string_view>) {
      return T{reinterpret_cast<const char*>(data_)};
    }
    else {
      return *reinterpret_cast<std::add_pointer_t<T>>(data_);
    }
  }

 private:
  friend std::ostream &operator<<(std::ostream &os, const Attribute &attr) {
    switch (attr.type_) {
      case MNL_TYPE_U8:     os << attr.value<uint8_t>(); break;
      case MNL_TYPE_U16:    os << attr.value<uint16_t>(); break;
      case MNL_TYPE_U32:    os << attr.value<uint32_t>(); break;
      case MNL_TYPE_U64:    os << attr.value<uint64_t>(); break;
      case MNL_TYPE_STRING: os << attr.value<std::string_view>(); break;
      case MNL_TYPE_FLAG:   os << "flag???"; break;
      case MNL_TYPE_MSECS:  os << "msec???"; break;
      case MNL_TYPE_NESTED: os << "nested???"; break;
      case MNL_TYPE_NESTED_COMPAT: os << "nested-compat???"; break;
      case MNL_TYPE_NUL_STRING: os << attr.value<std::string_view>(); break;
      case MNL_TYPE_BINARY: os << attr.value<void>(); break;
      default: break;
    }
    return os;
  }

 private:
  const E id_;
  const mnl_attr_data_type type_;
  void * const data_;
};

template <tuple_enum_t E, E ATTR>
struct CAttribute : Attribute<E> {
  decltype(auto) value() const {
    return Attribute<E>::template value<typename AttrTraits<E, ATTR>::type>();
  };
};


template <tuple_enum_t E = ctattr_type>
class Tuple : public std::map<E, Attribute<E>> {
  using base_t = std::map<E, Attribute<E>>;
  static constexpr auto max_index = TupleTraits<E>::max_index;
  static constexpr auto attr_names_ {
    __attribute_names<E>(std::make_index_sequence<max_index>{})
  };

 public:
  inline static constexpr auto attr_names(auto idx) { return attr_names_[idx]; }

  Tuple() = delete;
  Tuple(std::shared_ptr<buf_t> pbuf, const nlmsghdr * const nlh) : buf_(pbuf) {
    auto parser = [&] (const nlattr * const hdr) {
      if (mnl_attr_type_valid(hdr, max_index) < 0) [[unlikely]]
        throw std::invalid_argument {"invalid atribute index"};

      const auto id = (static_cast<E>(mnl_attr_get_type(hdr)));
      base_t::emplace(id, *hdr);
      return MNL_CB_OK;
    };
    mnl_attr_parse(
      nlh,
      sizeof(nfgenmsg),
      [] (const nlattr * const hdr, void *data) {
        return (*static_cast<decltype(parser)*>(data))(hdr);
      },
      &parser);
  }

 private:
  std::shared_ptr<buf_t> buf_;
};

template <> struct TupleTraits<ctattr_type> {
  static constexpr size_t max_index = CTA_MAX;
};
template <> struct AttrTraits<ctattr_type, CTA_TUPLE_ORIG> {
  using type = ctattr_tuple;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_NESTED;
  static constexpr std::string_view name {"CTA_TUPLE_ORIG"};
};
template <> struct AttrTraits<ctattr_type, CTA_TUPLE_REPLY> {
  using type = ctattr_tuple;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_NESTED;
  static constexpr std::string_view name {"CTA_TUPLE_REPLY"};
};
template <> struct AttrTraits<ctattr_type, CTA_STATUS> {
  using type = ctattr_tuple;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_UNSPEC;  // XXX:
  static constexpr std::string_view name {"CTA_STATUS"};
};
template <> struct AttrTraits<ctattr_type, CTA_PROTOINFO> {
  using type = ctattr_protoinfo;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_NESTED;  // XXX:
  static constexpr std::string_view name {"CTA_PROTOINFO"};
};
template <> struct AttrTraits<ctattr_type, CTA_TIMEOUT> {
  using type = uint32_t;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_U32;
  static constexpr std::string_view name {"CTA_TIMEOUT"};
};
template <> struct AttrTraits<ctattr_type, CTA_MARK> {
  using type = uint32_t;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_U32;
  static constexpr std::string_view name {"CTA_MARK"};
};
template <> struct AttrTraits<ctattr_type, CTA_USE> {
  using type = ctattr_tuple;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_UNSPEC; // XXX:
  static constexpr std::string_view name {"CTA_USE"};
};
template <> struct AttrTraits<ctattr_type, CTA_ID> {
  using type = ctattr_tuple;
  static constexpr mnl_attr_data_type type_index = MNL_TYPE_UNSPEC; // XXX:
  static constexpr std::string_view name {"CTA_ID"};
};

} // namespace tuple

class Conntrack : public Netfilter {
 public:
  inline void bind(const nfnetlink_groups groups,
                   const pid_t pid = MNL_SOCKET_AUTOPID) {
    Netfilter::bind(groups, pid);
  }

  inline auto list(const uint8_t domain = AF_INET) {

    Netfilter::bind(NFNLGRP_NONE, MNL_SOCKET_AUTOPID);

    const auto id = send<nfgenmsg>([&] (auto &nlhdr, auto &exthdr) {
      nlhdr.nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET;
      nlhdr.nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP;
      nlhdr.nlmsg_pid = getpid();

      exthdr.nfgen_family = domain;
      exthdr.version = NFNETLINK_V0;
      exthdr.res_id = 0;
    });

    std::vector<tuple::Tuple<ctattr_type>> tuples;
    if (auto pbuf = std::make_shared<buf_t>(recv()); pbuf) {
      auto callback = [&] (const nlmsghdr * const nlh) {
        tuples.emplace_back(pbuf, nlh);
        return MNL_CB_OK;
      };
      mnl_cb_run(
        pbuf->data(),
        pbuf->size(),
        id,
        portid(),
        [] (const nlmsghdr * const nlh, void * const data) {
          return (*static_cast<decltype(callback)*>(data))(nlh);
        },
        &callback);
    }
    return tuples;
  }
};

} // namespace conntrack
} // namespace netfilter
using netfilter::conntrack::Conntrack;

} // namespace netlink

TEST(netlink, socket)
{
  netlink::Conntrack ct{};

  const auto tuples = ct.list(AF_INET);

  for (const auto &t : tuples) {
    for(const auto &[id, attr] : t) {
      const auto &name = t.attr_names(id);
      std::clog
        << std::format("[{:2}] {:16}: ", static_cast<unsigned>(id), name)
        << attr << std::endl;
    }
  }
}

TEST(Attribute, attr)
{
  constexpr struct {
    nlattr hdr {
      .nla_len = sizeof(nlattr) + sizeof(data),
      .nla_type = CTA_TIMEOUT,
    };
    uint32_t data { 10 };
  } data;

  using netlink::netfilter::conntrack::tuple::Attribute;
  Attribute<ctattr_type> attr {data.hdr};
  EXPECT_EQ(attr.value<decltype(data.data)>(), data.data);

  using netlink::netfilter::conntrack::tuple::CAttribute;
  CAttribute<ctattr_type, static_cast<ctattr_type>(data.hdr.nla_type)> cattr {data.hdr};
  EXPECT_EQ(cattr.value(), data.data);
}
