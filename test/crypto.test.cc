#include <gh4ck3r/crypto.hh>
#include <gtest/gtest.h>

using Alg = gh4ck3r::crypto::Alg;
using Mode = gh4ck3r::crypto::Mode;

class OpenSSLTest : public ::testing::Test {
 protected:
  const std::vector<uint8_t> plaintext_ {
    'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'
  };
  const std::vector<uint8_t> ciphertext_ {
    0xd2, 0xaa, 0xe0, 0x8a, 0x08, 0x78, 0xc0, 0x0c,
    0x2f, 0xb3, 0x6f, 0xad, 0x0b, 0xf1, 0x75, 0x39,
  };
};

class OpenSSL1Test : public OpenSSLTest {
 protected:
  template <Alg alg, Mode mode>
  using Encryptor = gh4ck3r::crypto::openssl::v1::Encryptor<alg, mode>;

  template <Alg alg, Mode mode>
  using Decryptor = gh4ck3r::crypto::openssl::v1::Decryptor<alg, mode>;
};


TEST_F(OpenSSL1Test, SEED_CBC)
{
  EXPECT_EQ(ciphertext_, (Encryptor<Alg::SEED, Mode::CBC>{}
    .update(plaintext_.data(), plaintext_.size())
    .finalize()));

  EXPECT_EQ(plaintext_, (Decryptor<Alg::SEED, Mode::CBC>{}
    .update(ciphertext_.data(), ciphertext_.size())
    .finalize()));
}

#if OPENSSL_VERSION_NUMBER >= 0x030000000 // 3.0.0
class OpenSSL3Test : public OpenSSLTest {
 protected:
  using LIB_CTX = gh4ck3r::crypto::openssl::v3::LIB_CTX;

  template <Alg alg, Mode mode>
  using Encryptor = gh4ck3r::crypto::openssl::v3::Encryptor<alg, mode>;

  template <Alg alg, Mode mode>
  using Decryptor = gh4ck3r::crypto::openssl::v3::Decryptor<alg, mode>;
};

TEST_F(OpenSSL3Test, LIB_CTX)
{
  auto refcnt = LIB_CTX::use_count();
  {
    LIB_CTX ctx1;
    EXPECT_EQ(LIB_CTX::use_count(), ++refcnt);

    {
      LIB_CTX ctx2;
      EXPECT_EQ(LIB_CTX::use_count(), ++refcnt);

      EXPECT_EQ(0, OSSL_PROVIDER_available(nullptr, "legacy"));
      EXPECT_EQ(0, OSSL_PROVIDER_available(ctx2, "legacy"));
      const gh4ck3r::crypto::openssl::v3::Provider provider{"legacy"};
      EXPECT_EQ(1, OSSL_PROVIDER_available(nullptr, "legacy"));
      EXPECT_EQ(1, OSSL_PROVIDER_available(ctx2, "legacy"));
    }

    EXPECT_EQ(LIB_CTX::use_count(), --refcnt);
  }

  EXPECT_EQ(LIB_CTX::use_count(), --refcnt);
}

TEST_F(OpenSSL3Test, SEED_CBC)
{
  EXPECT_EQ(ciphertext_, (Encryptor<Alg::SEED, Mode::CBC>{}
    .update(plaintext_.data(), plaintext_.size())
    .finalize()));

  EXPECT_EQ(plaintext_, (Decryptor<Alg::SEED, Mode::CBC>{}
    .update(ciphertext_.data(), ciphertext_.size())
    .finalize()));
}
#endif
