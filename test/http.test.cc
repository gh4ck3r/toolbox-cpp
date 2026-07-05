#include <httplib.h>
#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <filesystem>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>

TEST(http, basic)
{
  const std::string host {"localhost"};
  constexpr in_port_t port {8080};

  const std::string content {"Hello World!"};
  httplib::Server svr;
  svr.Get("/hi", [&] (auto& req, auto& res) {
    res.set_content(content, "text/plain");
  });

  std::thread svrt { [&] { svr.listen(host, port); }};
  svr.wait_until_ready();

  httplib::Client cli {host, port};
  EXPECT_TRUE(cli.is_valid());

  const auto result = cli.Get("/hi");
  EXPECT_EQ(content, result->body);

  svr.stop();
  svrt.join();
}

namespace openssl::inline v3 {

struct Error : std::runtime_error {
  Error(const std::string_view msg, const long err = ERR_get_error()) :
    runtime_error{make_error_message(msg, err)}
  {}

 private:
  static std::string make_error_message(const std::string_view msg, const long err) {
    const char* reason = ERR_reason_error_string(err);
    return std::string{msg} + ": " + (reason ? reason : "Unknown OpenSSL error");
  }
};

class KeyPair {
 public:
  KeyPair() : pkey_(EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", 2048))
  {
    if (!pkey_) throw Error {"Failed to generate RSA key pair"};
  }
  KeyPair(const KeyPair &) = delete;
  KeyPair &operator=(const KeyPair &) = delete;

  ~KeyPair() noexcept {
    if (pkey_) EVP_PKEY_free(pkey_);
  }

  enum class Key { PRIVATE, PUBLIC, };
  template <Key k>
  bool write(const std::filesystem::path &p) const {
    constexpr auto deleter = [] (BIO *p) { BIO_free_all(p); };
    std::unique_ptr<BIO, decltype(deleter)> key_file { BIO_new_file(p.c_str(), "w") };
    if (!key_file) throw Error {"failed to create a file to write key"};

    if constexpr (k == Key::PRIVATE) {
      return 1 == PEM_write_bio_PrivateKey(key_file.get(),
                                          pkey_,
                                          nullptr,
                                          nullptr,
                                          0,
                                          nullptr,
                                          nullptr);
    } else {
      return 1 == PEM_write_bio_PUBKEY(key_file.get(), pkey_);
    }
  }

  inline ::EVP_PKEY * get() const { return pkey_; }

 private:
  ::EVP_PKEY * const pkey_;
};

class X509 {
 public:
  enum class Version : long {
    V1 = X509_VERSION_1,
    V2 = X509_VERSION_2,
    V3 = X509_VERSION_3,
  };

  X509() : x509_(X509_new()) {
    if (!x509_) throw Error {"failed to create X.509 certificate: "};
  }
  X509(const X509 &) = delete;
  X509 &operator=(const X509 &) = delete;

  ~X509() noexcept {
    X509_free(x509_);
  }

  inline X509 &version(const Version v) {
    if (X509_set_version(x509_, static_cast<long>(v)) != 1) {
      throw Error {"failed to set X.509 version"};
    }
    return *this;
  }

  inline X509 &serial(long v) {
    const auto deleter = [](ASN1_INTEGER *p) { ASN1_INTEGER_free(p); };
    std::unique_ptr<ASN1_INTEGER, decltype(deleter)> serial {ASN1_INTEGER_new()};
    if (!serial) throw Error{"Failed to create INTEGER for X.509 serial"};

    if (!ASN1_INTEGER_set(serial.get(), v)) {
      throw Error {"Failed to set INTEGER for X.509 serial"};
    }

    if (!X509_set_serialNumber(x509_, serial.get())) {
      throw Error {"Failed to set X.509 serial"};
    }

    return *this;
  }

  inline X509 &valid_for(std::chrono::seconds seconds) {
    // Not Before
    if (!X509_gmtime_adj(X509_getm_notBefore(x509_), 0)) {
      throw Error {"failed to set X.509 validify for not before"};
    }

    // Not After
    if (!X509_gmtime_adj(X509_getm_notAfter(x509_), seconds.count())) {
      throw Error {"failed to set X.509 validify for not after"};
    }

    return *this;
  }

  inline auto subject() const { return X509_get_subject_name(x509_); }
  inline X509 &subject(const std::string &v) {
    if (!X509_NAME_add_entry_by_txt(subject(),
                                    "CN",
                                    MBSTRING_ASC,
                                    reinterpret_cast<const unsigned char*>(v.c_str()),
                                    -1,
                                    -1,
                                    0)) {
      throw Error {"Failed to set X.509 subject name"};
    }
    return *this;
  }

  inline X509 &issuer(const X509_NAME * const name) {
    if (!X509_set_issuer_name(x509_, name)) {
      throw Error {"failed to set X.509 issuer name"};
    }
    return *this;
  }

  inline X509 &pubkey(const KeyPair &key) {
    if (!X509_set_pubkey(x509_, key.get())) {
      throw Error {"failed to set X.509 public key"};
    }
    return *this;
  }

  inline X509 &sign(const KeyPair &key, const EVP_MD *md)
  {
    if (!X509_sign(x509_, key.get(), md)) {
      throw Error{"failed to sign X.509 certificate with given key"};
    }

    return *this;
  }

  bool write(const std::filesystem::path &p) const {
    constexpr auto deleter = [] (BIO *p) { BIO_free_all(p); };
    std::unique_ptr<BIO, decltype(deleter)> cert_file(BIO_new_file(p.c_str(), "w"));
    if (!cert_file) throw Error {"failed to create a file to write X.509 certificate"};

    return PEM_write_bio_X509(cert_file.get(), x509_) == 1;
  }

 private:
  ::X509 * const x509_;
};

/// Implementation of the following command to create a cert/key for the test
/// openssl req -x509 -newkey rsa:2048 -days 365 -nodes -keyout key.pem -out cert.pem -subj "/CN=mock-svr"
static bool create_self_signed_cert(const std::filesystem::path& key, const std::filesystem::path &cert) try
{
  using openssl::KeyPair;
  using openssl::X509;
  using std::chrono::days;

  // 1. EVP 키 페어 생성 (RSA 2048비트)
  KeyPair pkey{};

  // X509 인증서 구조체 생성
  X509 x509{};
  x509.version(X509::Version::V3)    // 버전 설정 (v3 = 2)
    .serial(1)                       // 시리얼 번호 설정
    .valid_for(days{365}) // 유효 기간 설정 (현재 시간 기준 365일)
    .subject("mock-svr")             // 주체명(Subject Name) 설정 (/CN=mock-svr)
    .issuer(x509.subject())       // Self-signed 이므로 발급자(Issuer)를 주체와 동일하게 설정
    .pubkey(pkey)                  // 공개키 매핑
    .sign(pkey, EVP_sha256());  // 3. 서명 (SHA256 사용)
  
  if (!x509.write(cert)) {                     // 4. 인증서 파일 출력 (-out cert.pem)
    throw Error {"failed to write certificate file"};
  }

  // 5. 개인 키 파일 출력 (-keyout key.pem)
  if (!pkey.write<KeyPair::Key::PRIVATE>(key)) {
    throw Error {"failed to write private key"};
  }

  return true;
} catch(const openssl::Error &e) {
  std::cerr << "Failed to create self-signed certificate: " << e.what() << std::endl;
  return false;
} catch(...) {
  std::cerr << "Failed to create self-signed certificate: Unknown exception" << std::endl;
  return false;
}

} // namespace openssl::v3

class HTTPS_TEST : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(openssl::create_self_signed_cert(svr_key_, svr_cert_));
    for (const auto &f : {svr_cert_, svr_key_}) {
      ASSERT_TRUE(std::filesystem::exists(f));
    }
  }
  static void TearDownTestSuite() {
    for (const auto &f : {svr_cert_, svr_key_}) {
      EXPECT_TRUE(std::filesystem::remove(f));
    }
  }

 protected:
  static const std::filesystem::path svr_cert_;
  static const std::filesystem::path svr_key_;
};

const std::filesystem::path HTTPS_TEST::svr_cert_ {"cert.pem"};
const std::filesystem::path HTTPS_TEST::svr_key_ {"key.pem"};

TEST_F(HTTPS_TEST, basic)
{
  const std::string host {"localhost"};
  constexpr in_port_t port {8080};

  const std::string content {"Hello World!"};

  httplib::SSLServer svr{svr_cert_.c_str(), svr_key_.c_str()};
  svr.Get("/hi", [&] (auto& req, auto& res) {
    res.set_content(content, "text/plain");
  });

  std::thread svrt { [&] { svr.listen(host, port); }};
  svr.wait_until_ready();

  httplib::SSLClient cli {host, port};
  EXPECT_TRUE(cli.is_valid());

  {
    const auto result = cli.Get("/hi");
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), httplib::Error::SSLServerVerification);
  }

  cli.enable_server_certificate_verification(false);
  const auto result = cli.Get("/hi");
  ASSERT_TRUE(result);
  EXPECT_EQ(content, result->body);

  svr.stop();
  svrt.join();
}
