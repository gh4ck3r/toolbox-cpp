#pragma once
#include <algorithm>
#include <ostream>
#include <streambuf>
#include <string_view>

namespace gh4ck3r {

class indent_ostreambuf : public std::streambuf {
 public:
  indent_ostreambuf(std::streambuf* sbuf)
    : sbuf(sbuf) , need_prefix(true), nindent(0)
  {}

 private:
  std::streambuf* sbuf;
  bool            need_prefix;

  inline int sync() override { return sbuf->pubsync(); }

  int overflow(int c) override {
    if (c != traits_type::eof()) {
      if (need_prefix && nindent) {
        static constexpr std::string_view spaces =
          "                                                                ";
        static_assert(!spaces.empty() && spaces.size() % 64 == 0,
                      "spaces buffer size must be a non-empty multiple of 64");

        size_t remaining = nindent;
        while (remaining > 0) {
          const size_t chunk = std::min(remaining, spaces.size());
          const auto nput = sbuf->sputn(spaces.data(), static_cast<std::streamsize>(chunk));
          if (nput < 0 || static_cast<size_t>(nput) != chunk) {
            return std::char_traits<char>::eof();
          }
          remaining -= chunk;
        }
      }
      need_prefix = c == '\n';
    }
    return sbuf->sputc(c);
  }

 protected:
  size_t nindent;
};

class indent_ostream : private virtual indent_ostreambuf, public std::ostream {
  static constexpr size_t indent_level_ = 2;
 public:
  indent_ostream() = delete;
  indent_ostream(std::ostream& out) : indent_ostreambuf(out.rdbuf())
    , std::ios(static_cast<std::streambuf*>(this))
    , std::ostream(static_cast<std::streambuf*>(this))
  {}
  virtual ~indent_ostream() = default;

  inline void indent(const size_t nlevel = 1) {
    nindent += nlevel * indent_level_;
  }
  inline void unindent(const size_t nlevel = 1) {
    nindent -= std::min(nindent, nlevel * indent_level_);
  }

  template<bool RIGHT>
  class do_indent {
   public:
    do_indent(const size_t nlevel = 1) : nlevel_(nlevel) {}
    std::ostream &operator()(std::ostream &os) const {
      if (auto ios = dynamic_cast<indent_ostream*>(&os); ios) {
        if constexpr (RIGHT) {
          ios->indent(nlevel_);
        } else {
          ios->unindent(nlevel_);
        }
      }
      return os;
    }

   private:
    size_t nlevel_;
    friend inline std::ostream &operator<<(std::ostream &os, do_indent d) {
      return d(os);
    }
  };
};

using Logger = indent_ostream;
using indent = Logger::do_indent<true>;
using unindent = Logger::do_indent<false>;

} // namespace gh4ck3r

