#pragma once
#include <ostream>
#include <streambuf>

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
        const std::string prefix(nindent, ' ');
        if (const auto nput = sbuf->sputn(prefix.data(), nindent);
            static_cast<size_t>(nput) != nindent)
        {
          return std::char_traits<char>::eof();
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
    nindent += nlevel * indent_level_; };
  inline void unindent(const size_t nlevel = 1) {
    nindent -= nlevel * indent_level_;
  };

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
