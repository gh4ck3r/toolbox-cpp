#pragma once
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>
#include <cuchar>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <openssl/seed.h>
#include "singleton.hh"

namespace gh4ck3r::crypto {

enum class Alg {
  SEED,
};

enum class Mode {
  CBC,
};

inline namespace openssl {

template <Alg alg, Mode mode> struct Info;
template <> struct Info<Alg::SEED, Mode::CBC> {
  static constexpr auto EVP = EVP_seed_cbc;
  static constexpr size_t block_siz = SEED_BLOCK_SIZE;
  static constexpr size_t key_siz = SEED_KEY_LENGTH;
};

struct ERR : std::runtime_error {
  template <class T>
  explicit ERR(T&& arg) :
    runtime_error(get_error_msg(std::forward<T>(arg))) {}

 private:
  template <class T>
  std::string get_error_msg(T&& arg) const {
    std::string buf(1024, 0x00);
    ERR_error_string_n(ERR_peek_last_error(), buf.data(), buf.size());
    if (const auto eos = buf.find('\0'); eos != buf.npos) {
      buf.resize(eos);
    }
    return buf + ": " + arg;
  }
};

// https://www.openssl.org/docs/man1.0.2/man3/OPENSSL_VERSION_NUMBER.html
#if OPENSSL_VERSION_NUMBER >= 0x030000000 // 3.0.0
inline namespace v3 {

using LIB_CTX = SharedSingleton<OSSL_LIB_CTX,
      OSSL_LIB_CTX_get0_global_default,
      OSSL_LIB_CTX_free>;

class Provider {
 public:
  Provider() = delete;
  Provider(const char *name) :
    ossl_provider_(OSSL_PROVIDER_load(libctx_, name))
  {}

  ~Provider() noexcept {
    OSSL_PROVIDER_unload(ossl_provider_);
  }

 private:
  LIB_CTX libctx_;
  OSSL_PROVIDER * const ossl_provider_;
};


template <Alg alg, Mode mode, bool Encrypt>
class Cipher {
  std::optional<Provider> ossl_provider_;

 public:
  using KEY = std::array<uint8_t, Info<alg, mode>::key_siz>;
  using IV  = std::array<uint8_t, Info<alg, mode>::block_siz>;

  explicit Cipher(const KEY &key = {0, }, const IV &iv = {0, }) :
    ctx_(EVP_CIPHER_CTX_new()),
    outbuf_offset_(0)
  {
    if constexpr (alg == Alg::SEED) { ossl_provider_.emplace("legacy"); }

    constexpr auto EVP_CryptInit = Encrypt ? EVP_EncryptInit : EVP_DecryptInit;
    EVP_CryptInit(ctx_, Info<alg, mode>::EVP(), key.data(), iv.data());
  }

  ~Cipher() noexcept {
    EVP_CIPHER_CTX_free(ctx_);
  }

  auto &update(const uint8_t *data, size_t len) {
    if (outbuf_.size() - outbuf_offset_ < len) {
      outbuf_.resize(std::max(outbuf_.capacity(), len) << 1);
    }

    const auto int_cutoff = [] (const size_t &n) {
      return static_cast<int>(
          std::min(n, static_cast<size_t>(std::numeric_limits<int>::max())));
    };
    for (auto cipherlen = int_cutoff(len);
        cipherlen;
        cipherlen = int_cutoff(len -= static_cast<size_t>(cipherlen)))
    {
      int32_t outl;
      constexpr auto EVP_CryptUpdate = Encrypt ? EVP_EncryptUpdate : EVP_DecryptUpdate;
      if (1 != EVP_CryptUpdate(ctx_,
            outbuf_.data() + outbuf_offset_,
            &outl,
            data,
            cipherlen))
      {
        throw ERR {"Failed to update cipher"};
      }
    }
    return *this;
  }

  const auto &finalize() {
    int32_t outl;
    constexpr auto EVP_CryptFinal = Encrypt ? EVP_EncryptFinal_ex : EVP_DecryptFinal_ex;
    if (1 != EVP_CryptFinal(ctx_, outbuf_.data() + outbuf_offset_, &outl))
      throw std::runtime_error/* ERR */ {"Failed to finalize"};
    outbuf_offset_ += static_cast<size_t>(outl);
    outbuf_.resize(outbuf_offset_);
    outbuf_.shrink_to_fit();
    return outbuf_;
  }

 private:
  EVP_CIPHER_CTX * const ctx_;
  std::vector<uint8_t> outbuf_;
  size_t outbuf_offset_;
};

template <Alg alg, Mode mode>
using Encryptor = Cipher<alg, mode, true>;

template <Alg alg, Mode mode>
using Decryptor = Cipher<alg, mode, false>;

} // namespace v3
#endif

namespace v1 {

template <Alg alg, Mode mode, bool Encrypt>
class Cipher {
#if OPENSSL_VERSION_NUMBER >= 0x030000000 // 3.0.0
  std::optional<Provider> ossl_provider_;
#endif

 public:
  using KEY = std::array<uint8_t, Info<alg, mode>::key_siz>;
  using IV  = std::array<uint8_t, Info<alg, mode>::block_siz>;
  explicit Cipher(const KEY &key = {0, }, const IV &iv = {0, }) :
    ctx_(EVP_CIPHER_CTX_new()),
    outbuf_offset_(0)
  {
    if constexpr (alg == Alg::SEED) {
      ossl_provider_.emplace("legacy");
    }

    if (!ctx_) throw ERR {"Failed to create cipher context"};
    if (1 != EVP_CipherInit(ctx_,
          Info<alg, mode>::EVP(),
          key.data(),
          iv.data(),
          Encrypt ? 1 : 0)) {
      throw ERR {"Failed to initialize context"};
    }
  }

  ~Cipher() { EVP_CIPHER_CTX_free(ctx_); }

  auto &update(const uint8_t *data, size_t len) {
    if (outbuf_.size() - outbuf_offset_ < len) {
      outbuf_.resize(std::max(outbuf_.capacity(), len) << 1);
    }

    const auto int_cutoff = [] (const size_t &n) {
      return static_cast<int>(
          std::min(n, static_cast<size_t>(std::numeric_limits<int>::max())));
    };
    for (auto cipherlen = int_cutoff(len);
        cipherlen;
        cipherlen = int_cutoff(len -= static_cast<size_t>(cipherlen)))
    {
      int32_t outl;
      if (1 != EVP_CipherUpdate(ctx_,
            outbuf_.data() + outbuf_offset_,
            &outl,
            data,
            cipherlen))
      {
        throw ERR {"Failed to update cipher"};
      }
    }
    return *this;
  }

  const auto &finalize() {
    int32_t outl;
    if (1 != EVP_CipherFinal(ctx_, outbuf_.data() + outbuf_offset_, &outl))
      throw ERR {"Failed to finalize"};
    outbuf_offset_ += static_cast<size_t>(outl);
    outbuf_.resize(outbuf_offset_);
    outbuf_.shrink_to_fit();
    return outbuf_;
  }

 private:
  EVP_CIPHER_CTX * const ctx_;
  std::vector<uint8_t> outbuf_;
  size_t outbuf_offset_;
};

template <Alg alg, Mode mode>
using Encryptor = v1::Cipher<alg, mode, true>;

template <Alg alg, Mode mode>
using Decryptor = v1::Cipher<alg, mode, false>;

} // namespace v1

} // namespace openssl

} // namespace gh4ck3r::crypto
