#include <algorithm>
#include <arpa/inet.h> // ntohs
#include <array>
#include <bit>
#include <cstdio>
#include <ctime>
#include <format>
#include <gtest/gtest.h>
#include <iostream>
#include <span>
#include <variant>
#include <vector>

#pragma pack(push, 1)
typedef struct EtherHeader {
  uint8_t dstMac[6];
  uint8_t srcMac[6];
  uint16_t type;
} EtherHeader;

typedef struct IpHeader {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  uint8_t version : 4, header_length : 4;
#else
  uint8_t header_length : 4, version : 4;
#endif
  uint8_t tos;
  uint16_t length;
  uint16_t id;
  uint16_t fragOffset;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t checksum;
  uint8_t srcIp[4];
  uint8_t dstIp[4];
} IpHeader;
#pragma pack(pop)

namespace network {

using Octet = uint8_t;

template <typename T>
concept OctetCompat = requires {
  // std::is_same_v<std::decay_t<T>, Octet>;
  sizeof(T) == sizeof(Octet);
};

template <OctetCompat T, size_t Extent = std::dynamic_extent>
struct PacketV
    : std::span<std::conditional_t<std::is_const_v<T>, const Octet, Octet>,
                Extent> {
  using std::span<std::conditional_t<std::is_const_v<T>, const Octet, Octet>,
                  Extent>::span;
  PacketV() = delete;
};

#if 1
template <std::ranges::contiguous_range _Range>
PacketV(_Range &&) -> PacketV<
    std::remove_reference_t<std::ranges::range_reference_t<_Range &>>>;
#endif

template <typename HEADER>
class PacketView : std::span<const Octet, sizeof(std::decay_t<HEADER>)> {
  using base_t = std::span<const Octet, sizeof(std::decay_t<HEADER>)>;

public:
  template <typename... ARGS>
  PacketView(ARGS &&...args) : base_t(std::forward<ARGS>(args)...) {}
  inline PacketView(const Octet *p) : PacketView(p, sizeof(HEADER)) {}
  inline PacketView(Octet *p) : PacketView(const_cast<const Octet *>(p)) {}
  using typename base_t::span;

protected:
  inline auto &hdr() const {
    using hdr_ref_t = std::add_lvalue_reference_t<std::add_const_t<HEADER>>;
    return reinterpret_cast<hdr_ref_t>(base_t::front());
  }
};

template <std::integral T> constexpr T ntoh(const T v) {
  if constexpr (std::endian::native == std::endian::big)
    return v;
  else if constexpr (sizeof(T) == 2)
    return htons(v);
  else if constexpr (sizeof(T) == 4)
    return htonl(v);
  else if constexpr (std::endian::native == std::endian::little) {
    // std::byteswap
    static_assert(std::has_unique_object_representations_v<T>,
                  "T may not have padding bits");
    auto value_representation =
        std::bit_cast<std::array<std::byte, sizeof(T)>>(v);
    std::ranges::reverse(value_representation);
    return std::bit_cast<T>(value_representation);
  } else
    static_assert(false);
};

namespace IP4 {

enum class Protocol : decltype(IpHeader::protocol) {
  ICMP = 0x01,
  TCP = 0x06,
  UDP = 0x11,
};

inline std::ostream &operator<<(std::ostream &os, Protocol proto) {
  switch (proto) {
    using enum Protocol;
  case ICMP:
    return os << "ICMP";
  case TCP:
    return os << "TCP";
  case UDP:
    return os << "UDP";
  }
  return os << "Unknown("
            << static_cast<std::underlying_type_t<Protocol>>(proto) << ')';
}

} // namespace IP4
} // namespace network

template <>
struct std::formatter<network::IP4::Protocol> : formatter<string_view> {
  template <typename FormatContext>
  auto format(network::IP4::Protocol p, FormatContext &ctx) const {
    const auto name = std::format("{}({})", (std::ostringstream{} << p).str(),
                                  static_cast<uint8_t>(p));
    return formatter<string_view>::format(name, ctx);
  }
};

namespace network {
namespace IP4 {

class AddressView : std::span<const Octet, 4> {
public:
  AddressView() = delete;
  template <typename... ARGS>
  constexpr AddressView(ARGS &&...addr) : span(std::forward<ARGS>(addr)...) {}

private:
  friend inline std::ostream &operator<<(std::ostream &os,
                                         const AddressView &addr) {
    return os << std::format("{}.{}.{}.{}", addr[0], addr[1], addr[2], addr[3]);
  }
};

class HeaderView : PacketView<::IpHeader> {
public:
  HeaderView() = delete;
  template <typename... ARGS>
  HeaderView(ARGS &&...args) : PacketView(std::forward<ARGS>(args)...) {}

  inline uint8_t version() const { return hdr().version; }
  inline uint8_t header_length() const { return hdr().header_length << 2; }
  inline uint8_t tos() const { return hdr().tos; }
  inline uint16_t total_length() const { return ntoh(hdr().length); }
  inline uint8_t ttl() const { return hdr().ttl; }
  inline Protocol protocol() const { return Protocol{hdr().protocol}; }
  inline uint16_t checksum() const { return ntoh(hdr().checksum); }
  inline AddressView src() const { return hdr().srcIp; }
  inline AddressView dst() const { return hdr().dstIp; }
};

void parse(const HeaderView &h) {
  std::clog << ">> parsing IPv4" << std::endl
            << std::format("IPv{}, IHL: {}, Total length: {}", h.version(),
                           h.header_length(), h.total_length())
            << std::endl
            << std::format("TTL: {}, Protocol: {}, Checksum: {:#04x}", h.ttl(),
                           h.protocol(), h.checksum())
            << std::endl
            << h.src() << " -> " << h.dst() << std::endl;
}

} // namespace IP4

namespace IP6 {
struct Header {}; // dummy

class HeaderView : PacketView<Header> {
public:
  template <typename... ARGS>
  HeaderView(ARGS &&...args) : PacketView(std::forward<ARGS>(args)...) {}
}; // TODO implement
void parse(const HeaderView &h) {
  // TODO implement
  std::cerr << "parse IPv6 header!!" << std::endl;
}
} // namespace IP6

namespace ethernet {

enum class Type : decltype(::EtherHeader::type) {
  IP = 0x0800,
  IPv6 = 0x86dd,
  NetBIOS = 0x8191,
  ARP = 0x0806,
};

class FrameView : PacketView<::EtherHeader> {
public:
  FrameView() = delete;
  template <typename... ARGS>
  FrameView(ARGS &&...args) : PacketView(std::forward<ARGS>(args)...) {}

  inline auto type() const { return Type{ntoh(hdr().type)}; };
  inline auto data() const {
    return reinterpret_cast<const Octet *>(&hdr() + 1);
  }
};

using L3PacketView = std::variant<IP4::HeaderView, IP6::HeaderView>;

const L3PacketView parse(const FrameView &h) {
  std::clog << ">> parsing Ethernet Frame" << std::endl;
  switch (h.type()) {
    using enum Type;
  case IP:
    std::clog << "  type: IP\n" << std::string(40, '-') << std::endl;
    return IP4::HeaderView{h.data()};
  case IPv6:
    std::clog << "  type: IPv6" << std::string(40, '-') << std::endl;
    return IP6::HeaderView{h.data()};
  default: //  TODO add types
    throw std::runtime_error{"Unhandled ethernet frame type"};
  }
}
} // namespace ethernet

} // namespace network

TEST(Network, basic) {
  using network::Octet;
  std::vector<Octet> buffer{
      /// Ethernet header
      0xbe,
      0xef,
      0x00,
      0x00,
      0xca,
      0xfe, // dest MAC
      0xca,
      0xfe,
      0x00,
      0x00,
      0xbe,
      0xef, // src MAC
      0x08,
      0x00, // Type: IP

      // IP Header
      0x40 /*version*/ | 0x05 /*header length*/,
      0x00, // TOS
      0x05,
      0xdc, // Total Length(1500)
      0xaa,
      0xbb, // Identification
      0x40,
      0x00, // flag/fragment offset
      0x80, // TTL
      0x06, // protocol: TCP
      0xab,
      0xcd, // checksum
      192,
      168,
      0,
      1, // src ip
      192,
      168,
      0,
      254, // dst ip
  };

#if 1
  [[maybe_unused]] const std::vector<uint8_t> v{1, 2, 3, 4, 5};
  [[maybe_unused]] std::span s{v};
  [[maybe_unused]] network::PacketV p{v};
  static_assert(std::is_same_v<std::vector<Octet>::value_type, Octet>);
  static_assert(
      std::is_same_v<const std::vector<Octet>::value_type, const Octet>);
#else
  std::visit([](auto &l3pkt) { return parse(l3pkt); },
             parse(network::ethernet::FrameView{buffer}));
#endif
}
