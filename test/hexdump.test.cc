#include "hexdump.hh"
#include <gtest/gtest.h>
#include <array>
#include <sstream>
#include <vector>

using gh4ck3r::hexdump;

TEST(hexdump, ptr_len)
{
  constexpr char buf[] {"hello world"};
  const auto output = [] (const void * const p) {
    std::ostringstream oss;
    oss << p
      <<  "  68 65 6c 6c 6f 20 77 6f 72 6c 64 00              hello world.\n";
    return oss.str();
  }(buf);

  EXPECT_EQ(output, hexdump(buf, sizeof(buf)));
}

TEST(hexdump, empty)
{
  constexpr char buf[] {};
  EXPECT_TRUE(hexdump(buf, sizeof(buf)).empty());
}

TEST(hexdump, empty_stdarray)
{
  constexpr std::array<uint8_t, 0> buf;
  EXPECT_TRUE(hexdump(buf).empty());
}

TEST(hexdump, empty_vector8)
{
  const std::vector<uint8_t> v;
  EXPECT_TRUE(hexdump(v).empty());
}

TEST(hexdump, multiline_full)
{
  constexpr uint8_t buf[] {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
    0xf, 0xe, 0xd, 0xc, 0xb, 0xa, 0x9, 0x8, 0x7, 0x6, 0x5, 0x4, 0x3, 0x2, 0x1, 0x0,
  };

  const auto output = [] (const void * const p) {
    std::ostringstream oss;
    oss << p
      <<  "  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f  ................\n"
      << static_cast<decltype(p)>(static_cast<const uint8_t*>(p) + 0x10)
      <<  "  0f 0e 0d 0c 0b 0a 09 08 07 06 05 04 03 02 01 00  ................\n";
    return oss.str();
  }(buf);

  EXPECT_EQ(output, hexdump(buf, sizeof(buf)));
}

TEST(hexdump, multiline_partial)
{
  constexpr uint8_t buf[] {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
    0xf, 0xe, 0xd, 0xc, 0xb, 0xa, 0x9, 0x8, 0x7, 0x6,
  };

  const auto output = [] (const void * const p) {
    std::ostringstream oss;
    oss << p
      <<  "  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f  ................\n"
      << static_cast<decltype(p)>(static_cast<const uint8_t*>(p) + 0x10)
      <<  "  0f 0e 0d 0c 0b 0a 09 08 07 06                    ..........\n";
    return oss.str();
  }(buf);

  EXPECT_EQ(output, hexdump(buf, sizeof(buf)));
}

TEST(hexdump, array)
{
  constexpr char buf[] {"hello world"};
  const auto output = [] (const void * const p) {
    std::ostringstream oss;
    oss << p <<  "  68 65 6c 6c 6f 20 77 6f 72 6c 64 00              hello world.\n";
    return oss.str();
  }(buf);

  EXPECT_EQ(output, hexdump(buf));
}

TEST(hexdump, string)
{
  const std::string buf {"hello world"};
  const auto output = [&buf] {
    std::ostringstream oss;
    oss << reinterpret_cast<const void*>(buf.data())
      <<  "  68 65 6c 6c 6f 20 77 6f 72 6c 64                 hello world\n";
    return oss.str();
  }();

  EXPECT_EQ(output, hexdump(buf));
}

TEST(hexdump, wstring)
{
  const std::wstring buf {L"hello world"};
  const auto output = [&buf] {
    std::ostringstream oss;
    oss << reinterpret_cast<const void*>(buf.data())
      <<  "  00000068 00000065 0000006c 0000006c  hell\n"
      << reinterpret_cast<const void*>(&buf.at(4))
      <<  "  0000006f 00000020 00000077 0000006f  o wo\n"
      << reinterpret_cast<const void*>(&buf.at(8))
      <<  "  00000072 0000006c 00000064           rld\n";
    return oss.str();
  }();

  EXPECT_EQ(output, hexdump(buf));
}

TEST(hexdump, utf_8)
{
  const std::string buf {"안녕하세요~"};
  const auto output = [&buf] {
    std::ostringstream oss;
    oss << reinterpret_cast<const void*>(buf.data())
      <<  "  ec 95 88 eb 85 95 ed 95 98 ec 84 b8 ec 9a 94 7e  ...............~\n";

    return oss.str();
  }();

  EXPECT_EQ(output, hexdump(buf));
}

TEST(hexdump, stdarray)
{
  constexpr std::array<uint8_t, 0x10> a {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf
  };

  const auto output = [] (const void * const p) {
    std::ostringstream oss;
    oss << p <<  "  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f  ................\n";
    return oss.str();
  }(a.data());

  EXPECT_EQ(output, hexdump(a));
}


TEST(hexdump, vector8)
{
  const std::vector<uint8_t> v {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
    0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf
  };

  const auto output = [] (const void * const p) {
    std::ostringstream oss;
    oss << p <<  "  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f  ................\n";
    return oss.str();
  }(v.data());

  EXPECT_EQ(output, hexdump(v));
}

TEST(hexdump, vector16)
{
  const std::vector<uint16_t> v {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf
  };

  const auto output = [] (const void * const p) {
    std::ostringstream oss;
    oss << p
      <<  "  0000 0001 0002 0003 0004 0005 0006 0007  ........\n"
      << static_cast<decltype(p)>(static_cast<const uint8_t*>(p) + 0x10)
      <<  "  0008 0009 000a 000b 000c 000d 000e 000f  ........\n";
    return oss.str();
  }(v.data());

  EXPECT_EQ(output, hexdump(v));
}
