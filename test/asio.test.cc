#include <asio.hpp>
#include <gtest/gtest.h>

TEST(asio, simple_timer)
{
  asio::io_context io;

  bool expired = false;
  asio::steady_timer timer{io, 1};
  timer.async_wait([&expired](const auto &ec) {
    expired = true;
    EXPECT_FALSE(ec);
  });

  io.run();
  EXPECT_TRUE(expired);
}
