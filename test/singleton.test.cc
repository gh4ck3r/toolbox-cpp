#include <gh4ck3r/singleton.hh>
#include <gtest/gtest.h>

#include <dlfcn.h>
#include <gnu/lib-names.h>

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
