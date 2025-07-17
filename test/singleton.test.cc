#include <gh4ck3r/singleton.hh>
#include <gtest/gtest.h>

#include <dlfcn.h>
#include <gnu/lib-names.h>

using gh4ck3r::SharedSingleton;

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
