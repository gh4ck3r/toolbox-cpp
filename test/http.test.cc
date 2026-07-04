#include <httplib.h>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <netinet/in.h>

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

TEST(http, basic_ssl)
{
  const std::string host {"localhost"};
  constexpr in_port_t port {8080};

  const std::string content {"Hello World!"};
  // XXX: Use the following command to create a test cert/key
  // openssl req -x509 -newkey rsa:2048 -days 365 -nodes -keyout key.pem -out cert.pem -subj "/CN=mock-svr"
  httplib::SSLServer svr{"cert.pem", "key.pem"};
  svr.Get("/hi", [&] (auto& req, auto& res) {
    res.set_content(content, "text/plain");
  });

  std::thread svrt { [&] { svr.listen(host, port); }};
  svr.wait_until_ready();

  httplib::SSLClient cli {host, port};
  EXPECT_TRUE(cli.is_valid());

  EXPECT_FALSE(cli.Get("/hi"));

  cli.enable_server_certificate_verification(false);
  const auto result = cli.Get("/hi");
  ASSERT_TRUE(result);
  EXPECT_EQ(content, result->body);

  svr.stop();
  svrt.join();
}
