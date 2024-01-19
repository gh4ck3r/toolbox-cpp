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
