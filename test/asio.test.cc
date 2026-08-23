#include <functional>
#include <iostream>
#include <asio.hpp>
#include <gtest/gtest.h>
#include <memory>

TEST(timer, synchronous)
{
  using namespace std::chrono_literals;
  using std::chrono::steady_clock;;
  asio::io_context io;

  const auto before = steady_clock::now();
  asio::steady_timer {io, 100ms}.wait();
  const auto after = steady_clock::now();

  EXPECT_GE(after - before, 100ms);
}

TEST(timer, asynchronous)
{
  using std::chrono::steady_clock;
  asio::io_context io;

  using namespace std::chrono_literals;
  static constexpr auto interval = 100ms;

  const auto before = steady_clock::now();
  asio::steady_timer timer{io, interval};
  timer.async_wait([&before](const asio::error_code &ec) {
    EXPECT_FALSE(ec);

    const auto after = steady_clock::now();
    EXPECT_GE(after - before, interval);
  });

  io.run();
  const auto after = steady_clock::now();
  EXPECT_GE(after - before, interval);
}

TEST(timer, comprehensive_handler)
{
  asio::io_context io;

  using namespace std::chrono_literals;
  static constexpr auto interval = 100ms;
  int count = 0;
  asio::steady_timer t {io, interval};
  
  using handler_t = void(asio::steady_timer &, int &);
  std::function<handler_t> print {
    [&print] (auto &t, auto &count) {
      if (4 < ++count) return;

      using namespace std::chrono_literals;
      t.expires_at(t.expiry() + interval);
      t.async_wait(std::bind(print,
                             std::ref(t),
                             std::ref(count)));
    }};

  t.async_wait(std::bind(print,
                         std::ref(t),
                         std::ref(count)));
  EXPECT_EQ(count, 0);

  io.run();
  EXPECT_EQ(count, 5);
}

TEST(timer, member_handler)
{
  using namespace std::chrono_literals;
  static constexpr auto interval = 100ms;

  class printer {
   public:
    printer(asio::io_context &io) : timer_{io, 1s}, count_(0) {
      timer_.async_wait([this] (const asio::error_code &ec) {print();});
    }
    ~printer() {
      EXPECT_EQ(count_, 5);
    }

    void print() {
      if (4 < ++count_) return;

      timer_.expires_at(timer_.expiry() + interval);
      timer_.async_wait(std::bind(&printer::print, this));
    }
   private:
    asio::steady_timer timer_;
    int count_;
  };

  asio::io_context io;
  printer p(io);
  io.run();
}

TEST(strand, basic)
{
  using namespace std::chrono_literals;
  static constexpr auto interval = 100ms;
  class printer {
   public:
    printer(asio::io_context &io) :
      strand_(asio::make_strand(io)),
      timer1_(io, interval),
      timer2_(io, interval),
      count_(0)
    {
      timer1_.async_wait(
        asio::bind_executor(strand_, std::bind(&printer::print1, this)));

      timer2_.async_wait(
        asio::bind_executor(strand_, [this] (const auto &ec) {print2();}));
    }

    ~printer() {
      EXPECT_EQ(count_, 10);
    }

    void print1()
    {
      if (10 <= count_) return;
      ++count_;

      timer1_.expires_after(interval);
      timer1_.async_wait(
        asio::bind_executor(strand_, [this] (const auto &ec) {print1();}));
    }

    void print2()
    {
      if (10 <= count_) return;
      ++count_;

      timer2_.expires_after(interval);
      timer2_.async_wait(
        asio::bind_executor(strand_, std::bind(&printer::print2, this)));
    }

   private:
    asio::strand<asio::io_context::executor_type> strand_;
    asio::steady_timer timer1_;
    asio::steady_timer timer2_;
    size_t count_;
  };

  asio::io_context io;
  printer p{io};
  std::thread t {[&] { io.run(); }};
  io.run();
  t.join();
}

using asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
  explicit Session(tcp::socket socket)
  : socket_(std::move(socket)) {}

  void start() {
    do_read();
  }

private:
  void do_read() {
    auto self(shared_from_this());
    socket_.async_read_some(
      asio::buffer(data_, max_length),
      [this, self](std::error_code ec, std::size_t length) {
        if (!ec) {
          do_write(length);
        }
      });
  }

  void do_write(std::size_t length) {
    auto self(shared_from_this());
    asio::async_write(
      socket_,
      asio::buffer(data_, length),
      [this, self](std::error_code ec, std::size_t /*length*/) {
        if (!ec) {
          do_read();
        }
      });
  }

  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
};

class Server {
public:
  Server(asio::io_context& io_context, short port)
  : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }

private:
  void do_accept() {
    acceptor_.async_accept(
      [this](std::error_code ec, tcp::socket socket) {
        if (!ec) {
          std::make_shared<Session>(std::move(socket))->start();
        }
        do_accept();
      });
  }

  tcp::acceptor acceptor_;
};

class Client : public std::enable_shared_from_this<Client> {
public:
  Client(asio::io_context &io_context, const std::string &host, const std::string &port)
    : socket_(io_context),
    resolver_(io_context)
  {
    endpoints_ = resolver_.resolve(host, port);
  }

  void start() {
    do_connect();
  }

private:
  void do_connect() {
    auto self {shared_from_this()};
    asio::async_connect(
      socket_, endpoints_,
      [this, self] (std::error_code ec, const tcp::endpoint& /*endpoint*/) {
        if (!ec) {
          std::cout << "Connected to server!" << std::endl;
          do_read_input();
        } else {
          std::cerr << "Connect failed: " << ec.message() << std::endl;
        }
      }
    );
  }

  void do_read_input() {
    std::cout << "Enter message (or 'quit' to exit): ";
    std::string line;
    if (std::getline(std::cin, line)) {
      if (line == "quit") {
        socket_.close();
        return;
      }
      do_write(line + "\n");
    }
  }

  void do_write(const std::string& msg) {
    out_message_ = msg;
    auto self(shared_from_this());
    asio::async_write(
      socket_,
      asio::buffer(out_message_),
      [this, self] (std::error_code ec, std::size_t /*length*/) {
        if (!ec) {
          do_read_reply();
        } else {
          std::cerr << "Write failed: " << ec.message() << std::endl;
        }
      });
  }

  void do_read_reply() {
    auto self(shared_from_this());
    socket_.async_read_some(
      asio::buffer(in_buffer_, max_length),
      [this, self](std::error_code ec, std::size_t length) {
        if (!ec) {
          std::cout << "Server reply: ";
          std::cout.write(in_buffer_, length);

          do_read_input();
        } else {
          std::cerr << "Read failed: " << ec.message() << std::endl;
        }
      });
  }

  tcp::socket socket_;
  tcp::resolver resolver_;
  tcp::resolver::results_type endpoints_;

  std::string out_message_;
  enum { max_length = 1<<10 };
  char in_buffer_[max_length];
};

TEST(asio, tcp)
{
  asio::io_context io_context;

  Server server {io_context, 12345};
  std::cout << "TCP Echo Server running on port 12345..." << std::endl;

  std::thread svr {[&io_context] { io_context.run(); }};

  {
    //asio::io_context io_context;

    auto client = std::make_shared<Client>(io_context, "localhost", "12345");
    client->start();

    io_context.run();
  }

  //io_context.stop();
  svr.join();

}
