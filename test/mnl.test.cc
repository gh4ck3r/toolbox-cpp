#include <array>
#include <iterator>
#include <map>
#include <random>
#include <system_error>
#include <type_traits>
#include <vector>
#include <libmnl/libmnl.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <gtest/gtest.h>

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
  template <size_t N>
  using buf_t = std::array<uint8_t, N>;

  template <uint16_t TYPE, uint16_t FLAGS, size_t N>
  auto netlink_header(buf_t<N> &buf) {
    static_assert(N > sizeof(nlmsghdr));

    nlmsghdr * const hdr = mnl_nlmsg_put_header(buf.data());
    hdr->nlmsg_type = TYPE;
    hdr->nlmsg_flags = FLAGS;
    hdr->nlmsg_seq = seq_++;


    return hdr;
  }
  template <typename EXTHDR>
  msgid_t sendmsg(auto callback) {
    buf_t<MNL_SOCKET_BUFFER_SIZE> buf;

    nlmsghdr * const nlhdr = mnl_nlmsg_put_header(buf.data());
    callback(*nlhdr, *reinterpret_cast<EXTHDR*>(
      mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg))));
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
class Tuple {
  std::array<nlattr* ,CTA_MAX + 1> buf_;
};

} // namespace tuple

enum class Subsystem : uint8_t {
  CTNETLINK_EXP     = NFNL_SUBSYS_CTNETLINK_EXP,
  CTNETLINK         = NFNL_SUBSYS_CTNETLINK,
  CTNETLINK_TIMEOUT = NFNL_SUBSYS_CTNETLINK_TIMEOUT,
  CTHELPER          = NFNL_SUBSYS_CTHELPER,
};

class Conntrack : public Netfilter {
 public:
  inline void bind(const nfnetlink_groups groups,
                   const pid_t pid = MNL_SOCKET_AUTOPID) {
    Netfilter::bind(groups, pid);
  }

  using enum Subsystem;
  inline auto list(const uint8_t domain = AF_INET) {
    return sendmsg<
      CTNETLINK,
      IPCTNL_MSG_CT_GET,
      NLM_F_REQUEST|NLM_F_DUMP>({
        .nfgen_family = domain,
        .version = NFNETLINK_V0,
        .res_id = 0,
    });
  }

  auto recvmsg(const msgid_t id) {
    buf_t<MNL_SOCKET_DUMP_SIZE> buf; // XXX
		auto sz = mnl_socket_recvfrom(socket(), buf.data(), buf.size());
  }

 protected:
  template <Subsystem SUBSYS, cntl_msg_types MSG, uint16_t FLAGS>
  msgid_t sendmsg(const nfgenmsg hdr) {
    buf_t<MNL_SOCKET_DUMP_SIZE> sndbuf;
    constexpr auto subsys =
      static_cast<std::underlying_type_t<Subsystem>>(SUBSYS);
    auto nlh = netlink_header<(subsys << 8) | MSG, FLAGS>(sndbuf);

    *reinterpret_cast<nfgenmsg*>(
      mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg))) = hdr;

    for (ssize_t nsent, nleft = nlh->nlmsg_len; nleft; nleft -= nsent) {
      nsent = mnl_socket_sendto(socket(), nlh, nlh->nlmsg_len);
      if (nsent == -1) [[unlikely]] throw std::system_error {
        errno,
        std::system_category(),
        "failed to send netlink msg"};
    }

    return nlh->nlmsg_seq;
  }
};

} // namespace conntrack
} // namespace netfilter
using netfilter::conntrack::Conntrack;

} // namespace netlink


TEST(netlink, socket)
{
  netlink::Conntrack ct{};
  ct.bind(NFNLGRP_NONE);

  const auto id = ct.list(AF_INET);

  ct.recvmsg(id);
}
