#include <unordered_map>
#include <unordered_set>
#include <gh4ck3r/hash.hh>
#include <gtest/gtest.h>

namespace gh4ck3r::whatever {

struct Person {
  std::string name;
  size_t age;
  std::optional<std::string> petname;
  uint8_t height;
};

inline std::ostream &operator<<(std::ostream &os, const Person &p) {
  os << "name: " << p.name << ", age: " << p.age;
  if (p.petname) os << ", petname: " << p.petname.value();

  return os;
}

inline bool operator==(const Person &lhs, const Person &rhs) {
  return lhs.name == rhs.name
      && lhs.age == rhs.age
      && lhs.height == rhs.height
      && lhs.petname == rhs.petname;
}

} // namespace gh4ck3r::whatever

template <>
struct std::hash<gh4ck3r::whatever::Person> {
  std::size_t operator()(const gh4ck3r::whatever::Person &p) const noexcept {
    return gh4ck3r::hash_combine(p.name, p.age, p.height, p.petname);
  }
};

TEST(hash, custom_type)
{
  gh4ck3r::whatever::Person p1 {"foo", 10}, p2 {"foo", 10}; 
  EXPECT_EQ(p1, p2);

  const std::hash<gh4ck3r::whatever::Person> hash;
  EXPECT_EQ(hash(p1), hash(p2));
}

TEST(hash, unordered_set)
{
  std::unordered_set<gh4ck3r::whatever::Person> s;

  s.insert({"foo", 10});
  EXPECT_EQ(s.size(), 1);

  s.insert({"foo", 10});
  EXPECT_EQ(s.size(), 1);

  s.insert({"foo", 10, "Foo"});
  EXPECT_EQ(s.size(), 2);
}

TEST(hash, unordered_map)
{
  std::unordered_map<gh4ck3r::whatever::Person, std::string> s;

  s.insert({{"foo", 10}, "first"});
  EXPECT_EQ(s.size(), 1);

  s.insert({{"foo", 10}, "second"});
  EXPECT_EQ(s.size(), 1);

  s.insert({{"foo", 10, "Foo"}, "third"});
  EXPECT_EQ(s.size(), 2);
}

enum class Level { low, medium, high };
std::ostream &operator<<(std::ostream &os, const Level &l) {
  switch (l) {
    case Level::low: return os << "low";
    case Level::medium: return os << "medium";
    case Level::high: return os << "high";
  }
  return os;
}

struct Tag {
  std::string type;
  std::unordered_set<Level> levels;
};

bool operator==(const Tag &lhs, const Tag &rhs)
{
  return lhs.type == rhs.type
      && lhs.levels == rhs.levels;
}

template <>
struct std::hash<Tag> {
  std::size_t operator()(const Tag &t) const noexcept {
    return gh4ck3r::unordered_hash_combine(t.levels) ^
        gh4ck3r::hash_combine(t.type);
  }
};

TEST(hash, unordered_set_enum)
{
  using enum Level;
  std::unordered_set<Tag> tags {
    {"foo", {}},
  };

  auto [_, inserted] = tags.insert({"foo", {}});
  EXPECT_FALSE(inserted);

  std::tie(_, inserted) = tags.insert({"foo", {low}});
  EXPECT_TRUE(inserted);

  std::tie(_, inserted) = tags.insert({"foo", {low}});
  EXPECT_FALSE(inserted);

  std::tie(_, inserted) = tags.insert({"foo", {low, low}});
  EXPECT_FALSE(inserted);

  std::tie(_, inserted) = tags.insert({"foo", {low, low, low}});
  EXPECT_FALSE(inserted);

  std::tie(_, inserted) = tags.insert({"foo", {low, medium}});
  EXPECT_TRUE(inserted);

  std::tie(_, inserted) = tags.insert({"foo", {medium, low, medium, low}});
  EXPECT_FALSE(inserted);
}
