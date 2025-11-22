#include <random>
#include <system_error>
#include <type_traits>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <gtest/gtest.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#include <libmnl/libmnl.h>

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

template <typename T>
concept attr_type_t = std::integral<T> || std::is_enum_v<T>;

struct alignas(NLA_ALIGNTO) AttrHdr : nlattr {
  static_assert(sizeof(nlattr) == NLA_HDRLEN);

  AttrHdr() = delete;
  AttrHdr(nlattr attr) : nlattr(attr) {
    if (nla_len < NLA_HDRLEN) nla_len = NLA_HDRLEN;
  }

 protected:
  template <typename R = void>
  inline std::add_pointer_t<R> data() const {
    if (nla_len <= NLA_HDRLEN) [[unlikely]] return nullptr;

    return reinterpret_cast<std::add_pointer_t<R>>(
      const_cast<uint8_t*>(
        reinterpret_cast<const uint8_t*>(this) + NLA_HDRLEN));
  }
};

template <typename T>
class alignas(alignof(AttrHdr)) AttrData {
  struct Empty {};
 protected:
  [[no_unique_address]] std::conditional_t<std::is_void_v<T>, Empty, T> value_;
  static constexpr size_t len = [] {
    if constexpr (std::is_void_v<T>)
      return 0;
    else
      return sizeof(T);
  }();

  AttrData() {
    static_assert(std::is_void_v<T>);
  };
  template <typename...ARGS>
  requires std::is_constructible_v<T, ARGS...>
  AttrData(ARGS&&...args) :
    value_(std::forward<ARGS>(args)...)
  {}

 public:
  using value_type = T;
};

template <attr_type_t auto ATTR>
struct AttrTraits { using type = void; };

template <attr_type_t auto ATTR>
struct alignas(alignof(AttrHdr)) Attribute : AttrHdr, AttrData<typename AttrTraits<ATTR>::type>
{
  using body_t = AttrData<typename AttrTraits<ATTR>::type>;
  template <typename...ARGS>
  Attribute(ARGS&&...args) :
    AttrHdr({
      .nla_len = NLA_HDRLEN + body_t::len,
      .nla_type = ATTR,
    }),
    body_t(std::forward<ARGS>(args)...)
  {
  }

  auto &value() const { return *static_cast<std::add_pointer_t<typename body_t::value_type>>(data()); };
};

struct alignas(NLMSG_ALIGNTO) Message {
  Message(nlmsghdr hdr);
  template<typename EXTHDR>
  Message &&extra_header(EXTHDR) &&;

  template <attr_type_t auto ATTR>
  Message &&attr(Attribute<ATTR>) &&;
};

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

  #if 0
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
  #endif

  #if 0
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
  #endif

 private:
  mnl_socket * const socket_;
  msgid_t seq_;
};

template <> struct AttrTraits<CTA_STATUS> { using type = uint32_t; };
template <> struct AttrTraits<CTA_TIMEOUT> { using type = uint32_t; };

template <> struct AttrTraits<CTA_IP_V4_SRC> { using type = in_addr_t; };
template <> struct AttrTraits<CTA_IP_V4_DST> { using type = in_addr_t; };

} // namespace netlink

class NetlinkAttributeTest : public ::testing::Test {
 protected:
  using buf_t = std::vector<uint8_t>;

  template <netlink::attr_type_t auto ATTR>
  static constexpr auto make_buf() {
    using data_t = netlink::AttrTraits<ATTR>::type;
    if constexpr (std::is_void_v<data_t>)
      return buf_t (NLA_HDRLEN);
    else
      return buf_t (NLA_HDRLEN + NLA_ALIGN(sizeof(data_t)));
  }

  template <typename T, typename...ARGS>
  T& make_attr(buf_t &buf, ARGS&&...args) {
    void *ptr = buf.data();
    auto size = buf.size();
    if (!std::align(std::alignment_of_v<T>, sizeof(T), ptr, size)) {
      ADD_FAILURE() << "failed to align attribute buffer";
    }
    EXPECT_EQ(size, buf.size());

    return *new (ptr) T {std::forward<ARGS>(args)...};
  }

  template <netlink::attr_type_t auto ATTR>
  void test_build_attr() {
    using attr_t = netlink::Attribute<ATTR>;
    static_assert(std::is_void_v<typename attr_t::value_type>);

    auto buf = make_buf<ATTR>();
    auto &attr = make_attr<attr_t>(buf);

    EXPECT_EQ(attr.nla_len, sizeof(attr_t));
    EXPECT_EQ(attr.nla_len, NLA_HDRLEN);
    EXPECT_EQ(attr.nla_type, ATTR);
  }

  template <netlink::attr_type_t auto ATTR>
  void test_build_attr(const netlink::Attribute<ATTR>::value_type value) {
    using attr_t = netlink::Attribute<ATTR>;
    static_assert(!std::is_void_v<typename attr_t::value_type>);

    auto buf = make_buf<ATTR>();
    auto &attr = make_attr<attr_t>(buf, value);

    EXPECT_EQ(attr.nla_len, sizeof(attr_t));
    EXPECT_EQ(attr.nla_len, NLA_HDRLEN + sizeof(typename attr_t::value_type));
    EXPECT_EQ(attr.nla_type, ATTR);
    EXPECT_EQ(attr.value(), value);
  }
};

struct Buffer : std::vector<uint8_t> {
  Buffer() {
    resize(NLA_HDRLEN + NLA_ALIGN(sizeof(in_addr_t)));
  }
};

TEST_F(NetlinkAttributeTest, AttributeStaticAsserts)
{
  using netlink::AttrHdr;
  static_assert(!std::is_constructible_v<AttrHdr>);
  static_assert(std::is_constructible_v<AttrHdr, nlattr>);
}

TEST_F(NetlinkAttributeTest, AttributeDefault)
{
  struct Attribute : netlink::AttrHdr {
    Attribute(nlattr attr) : netlink::AttrHdr{attr} {}
    using netlink::AttrHdr::data;
  };

  Attribute a {{}}; 

  EXPECT_EQ(a.nla_len, sizeof(nlattr));
  EXPECT_EQ(a.nla_type, 0);
  EXPECT_EQ(a.data(), nullptr);
  EXPECT_EQ(sizeof(a), sizeof(nlattr));
}

TEST_F(NetlinkAttributeTest, build_Attribute_UNSPEC)
{
  SCOPED_TRACE("Testing CTA_UNSPEC");
  test_build_attr<CTA_UNSPEC>();
}

TEST_F(NetlinkAttributeTest, build_Attribute_CTA_STATUS)
{
  const uint32_t value = 30;
  SCOPED_TRACE("Testing CTA_STATUS");
  test_build_attr<CTA_STATUS>(value);
}

TEST_F(NetlinkAttributeTest, build_Attribute_CTA_TIMEOUT)
{
  const uint32_t value = 1000;
  SCOPED_TRACE("Testing CTA_TIMEOUT");
  test_build_attr<CTA_TIMEOUT>(value);
}

TEST_F(NetlinkAttributeTest, build_Attribute_CTA_IP_V4_SRC)
{
  const auto addr {inet_addr("1.1.1.1")};
  SCOPED_TRACE("Testing CTA_IP_V4_SRC");
  test_build_attr<CTA_IP_V4_SRC>(addr);
}

TEST_F(NetlinkAttributeTest, build_Attribute_CTA_IP_V4_DST)
{
  const auto addr {inet_addr("2.2.2.2")};
  SCOPED_TRACE("Testing CTA_IP_V4_DST");
  test_build_attr<CTA_IP_V4_DST>(addr);
}


#if 0
TEST_F(NetlinkAttributeTest, build_Message)
{
  using namespace netlink;
  uint16_t i = 80;

  uint32_t seq = std::mt19937{std::random_device{}()}();
  [[maybe_unused]] auto m = Message {{
      .nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW,
      .nlmsg_flags = NLM_F_REQUEST|NLM_F_CREATE|NLM_F_EXCL|NLM_F_ACK,
      .nlmsg_seq = seq,
    }}
    .extra_header<nfgenmsg>({
      .nfgen_family = AF_INET,
      .version = NFNETLINK_V0,
      .res_id = 0,
    })
    .attr<CTA_TUPLE_ORIG>({
      Attribute<CTA_TUPLE_IP> {
        Attribute<CTA_IP_V4_SRC>{inet_addr("1.1.1.1")},
        Attribute<CTA_IP_V4_DST>{inet_addr("2.2.2.2")}
      },
      Attribute<CTA_TUPLE_PROTO> {
        Attribute<CTA_PROTO_NUM>(IPPROTO_TCP),
        Attribute<CTA_PROTO_SRC_PORT>(htons(i)),
        Attribute<CTA_PROTO_DST_PORT>(htons(1025))
      }
    })
    .attr<CTA_TUPLE_REPLY>([&] (auto &reply) {
      reply.template attr<CTA_TUPLE_IP>([] (auto &ip) {
        ip.template attr<CTA_IP_V4_SRC>(inet_addr("2.2.2.2"))
          .template attr<CTA_IP_V4_DST>(inet_addr("1.1.1.1"))
          ;
      });
      reply.template attr<CTA_TUPLE_PROTO>([&] (auto &proto) {
        proto
          .template attr<CTA_PROTO_NUM>(IPPROTO_TCP)
          .template attr<CTA_PROTO_SRC_PORT>(htons(1025))
          .template attr<CTA_PROTO_DST_PORT>(htons(i))
          ;
      });
    })
    .attr<CTA_PROTOINFO>([] (auto &proto_info) {
      proto_info.template attr<CTA_PROTOINFO_TCP>([] (auto &tcp) {
        tcp.template attr<CTA_PROTOINFO_TCP_STATE>(TCP_CONNTRACK_SYN_SENT);
      });
    })
    .attr<CTA_STATUS>(htonl(IPS_CONFIRMED))
    .attr<CTA_TIMEOUT>(htonl(1000))
    ;

  // TODO: FIXME
  // const nlmsghdr * const buf = m;
}
#endif
