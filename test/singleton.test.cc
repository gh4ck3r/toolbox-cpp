#include <gh4ck3r/singleton.hh>
#include <gtest/gtest.h>

#include <dlfcn.h>
#include <gnu/lib-names.h>

#include <thread>
#include <vector>

using namespace gh4ck3r::singleton;

TEST(SharedSingleton, dl)
{
  constexpr auto dlopen = [] { return ::dlopen(LIBM_SO, RTLD_LAZY); };
  constexpr auto dlclose = [] (auto *p) { if (::dlclose(p)) FAIL(); };

  using libm = SharedSingleton<void, dlopen, dlclose>;

  EXPECT_EQ(0, libm::use_count());
  {
    libm dlhandle1;
    EXPECT_EQ(1, libm::use_count());
    EXPECT_EQ(1, dlhandle1.use_count());

    {
      libm dlhandle2;
      EXPECT_EQ(2, libm::use_count());
      EXPECT_EQ(2, dlhandle2.use_count());
      EXPECT_EQ(2, dlhandle2.use_count());

      EXPECT_EQ(::dlsym(dlhandle1, "cos"), ::dlsym(dlhandle2, "cos"));
    }
    EXPECT_EQ(1, libm::use_count());
    EXPECT_EQ(1, dlhandle1.use_count());
  }
  EXPECT_EQ(0, libm::use_count());
}

class SharedSingletonDefaultTest : public ::testing::Test {
 protected:
  struct MyService {
    int value = 42;
  };
  using Service = SharedSingleton<MyService>;
};

TEST_F(SharedSingletonDefaultTest, lifecycle)
{
  EXPECT_EQ(0, Service::use_count());
  {
    Service s1;
    EXPECT_EQ(1, Service::use_count());
    EXPECT_EQ(42, s1->value);

    {
      Service s2;
      EXPECT_EQ(2, Service::use_count());
      EXPECT_EQ(s1.get(), s2.get());
    }
    EXPECT_EQ(1, Service::use_count());
  }
  EXPECT_EQ(0, Service::use_count());
}

TEST_F(SharedSingletonDefaultTest, implicit_conversion)
{
  Service s;
  MyService *raw = s;
  EXPECT_EQ(s.get(), raw);
}

TEST(SharedSingleton, creation_failure)
{
  constexpr auto fail_create = [] () -> int* { return nullptr; };
  using Bad = SharedSingleton<int, fail_create>;
  EXPECT_THROW(Bad{}, std::runtime_error);
}

TEST(SharedSingleton, creation_failure_message_contains_type)
{
  constexpr auto fail_create = [] () -> int* { return nullptr; };
  using Bad = SharedSingleton<int, fail_create>;
  try {
    Bad{};  // NOLINT(bugprone-dangling-handle) - intentionally testing ctor throws
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("SharedSingleton"), std::string::npos);
    // int is a complete type, so error message should contain type name
    EXPECT_NE(msg.find(typeid(int).name()), std::string::npos);
  }
}

TEST(SharedSingleton, multithread)
{
  struct Counter { int value = 0; };
  using Svc = SharedSingleton<Counter>;

  constexpr int num_threads = 8;
  std::vector<std::thread> threads;

  EXPECT_EQ(0, Svc::use_count());
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([] {
      Svc s;
      EXPECT_NE(nullptr, s.get());
      // Hold it briefly to exercise concurrent access
      std::this_thread::yield();
    });
  }
  for (auto &t : threads) t.join();
  EXPECT_EQ(0, Svc::use_count());
}

class SingletonTraitsTest: public ::testing::Test {
 protected:
  class Descendant : SingletonTraits {};
};

TEST_F(SingletonTraitsTest, prohibit_ctor)
{
  static_assert(not std::is_constructible_v<SingletonTraits>);
}

TEST_F(SingletonTraitsTest, prohibit_copy)
{
  static_assert(not std::is_copy_constructible_v<Descendant>);
  static_assert(not std::is_copy_assignable_v<Descendant>);
}

TEST_F(SingletonTraitsTest, prohibit_move)
{
  class Foo : SingletonTraits {};
  static_assert(not std::is_move_constructible_v<Foo>);
  static_assert(not std::is_move_assignable_v<Foo>);
}

class StaticSingletonTest: public ::testing::Test {
 protected:
  class SomeType {};
};

TEST_F(StaticSingletonTest, prohibit_ctor)
{
  static_assert(not std::is_constructible_v<StaticSingleton<SomeType>>);
}

TEST_F(StaticSingletonTest, prohibit_copy)
{
  static_assert(not std::is_copy_constructible_v<StaticSingleton<SomeType>>);
  static_assert(not std::is_copy_assignable_v<StaticSingleton<SomeType>>);
}

TEST_F(StaticSingletonTest, prohibit_move)
{
  static_assert(not std::is_move_constructible_v<StaticSingleton<SomeType>>);
  static_assert(not std::is_move_assignable_v<StaticSingleton<SomeType>>);
}

TEST_F(StaticSingletonTest, prohibit_inheritance)
{
  static_assert(std::is_final_v<StaticSingleton<SomeType>>);
}

TEST_F(StaticSingletonTest, instance_returns_same_reference)
{
  auto &a = StaticSingleton<SomeType>::instance();
  auto &b = StaticSingleton<SomeType>::instance();
  EXPECT_EQ(&a, &b);
}

TEST_F(StaticSingletonTest, instance_preserves_state)
{
  struct Stateful { int counter = 0; };
  auto &s = StaticSingleton<Stateful>::instance();
  int before = s.counter;
  s.counter += 10;
  auto &s2 = StaticSingleton<Stateful>::instance();
  EXPECT_EQ(before + 10, s2.counter);
}
