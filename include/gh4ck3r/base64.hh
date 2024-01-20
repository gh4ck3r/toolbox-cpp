#pragma once
#include <iterator>
#include <sstream>
#include <string_view>
#include <vector>
#include <cstdint>

namespace gh4ck3r::base64 {

#pragma pack(1)
union unit {
    uint8_t t[3];
    struct {
        std::remove_extent_t<decltype(t)>
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            c4:6, c3:6, c2:6, c1:6;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            c1:6, c2:6, c3:6, c4:6;
#else
#error Check endianess
#endif
    } q;
};
#pragma pack()
static_assert(sizeof(unit) == 3, "gh4ck3r::base64::unit MUST be packaged");

template<class It>
std::string encode(It beg, const It end)
{
    static_assert(1 == sizeof(*beg));
    constexpr std::string_view enc_tbl {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/"
    };

    unit unit;
    std::ostringstream os;
    const auto last {std::prev(end)};
    for (size_t i = 2; beg != end; i ? --i : i = 2, std::advance(beg, 1)) {
        unit.t[i] = *beg;

        if (!i || beg == last) {
            const auto &q = unit.q;
            os  << enc_tbl[q.c1]
                << enc_tbl[q.c2]
                << (i == 2 ? '=' : enc_tbl[q.c3])
                << (i  > 0 ? '=' : enc_tbl[q.c4]);
            unit = {{}};
        }
    }

    return os.str();
}

template<class T>
T decode_as(const std::string_view b64str)
{
    constexpr char dec_tbl[] {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 00-0F */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 10-1F */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, /* 20-2F */
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, /* 30-3F */
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, /* 40-4F */
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63, /* 50-5F */
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, /* 60-6F */
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, /* 70-7F */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 80-8F */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 90-9F */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* A0-AF */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* B0-BF */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* C0-CF */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* D0-DF */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* E0-EF */
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  /* F0-FF */
    };

    T ret;
    if constexpr (std::conjunction_v<
            std::is_same<T, std::vector<typename T::value_type>>,
            std::is_same<T, std::string>>)
    {
        ret.reserve(b64str.size() * 3 / 4);
    }

    unit u;
    size_t n {0};
    for (const auto &c : b64str) {
        if (c == '=') break;

        const auto d {dec_tbl[static_cast<size_t>(c)]};
        if (d < 0) throw std::invalid_argument {
            "trying to decode invalid base64 string : " + std::string{b64str}};

        auto &q = u.q;
        constexpr decltype(q.c1) mask = (1<<6)-1;
        switch (n) {
            case 0: q.c1 = d & mask; break;
            case 1: q.c2 = d & mask; break;
            case 2: q.c3 = d & mask; break;
            case 3: q.c4 = d & mask; break;
        }

        if (n++) ret.insert(ret.end(), u.t[4 - n]);
        if (!(n %= 4)) u = {{}};
    }
    return ret;
}

template <class T, class = std::enable_if_t<std::is_object_v<T>>>
inline std::string encode(T&& data) {
    return encode(begin(std::forward<T>(data)), end(std::forward<T>(data)));
}

template <size_t N>
inline std::string encode(const char (&lit)[N]) {
    static_assert(N);
    return encode(lit, lit + N - 1);
}

inline std::string decode(const std::string_view base64_str) {
    return decode_as<decltype(decode(base64_str))>(base64_str);
}

} // namespace gh4ck3r::base64
