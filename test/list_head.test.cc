#include "gh4ck3r/list_head.hh"
#include <gtest/gtest.h>

struct list_head_test : ::testing::Test {
 protected:
  struct Node {
      int id;
      std::string name;
      list_head list;
  };

  void add_tail(list_head* head, list_head* new_node)
  {
    list_head* prev = head->prev;
    new_node->next = head;
    new_node->prev = prev;
    prev->next = new_node;
    head->prev = new_node;
  };
};

TEST_F(list_head_test, iterator_v1)
{
  std::array nodes {
    Node {1, "First node", {}},
    Node {2, "Second node", {}},
    Node {3, "Third node", {}},
  };

  list_head my_list { &my_list, &my_list };
  for (auto &n : nodes) add_tail(&my_list, &n.list);

  auto arr_iter = nodes.begin();

  using gh4ck3r::c_compat::v1::list_head_iterator;
  for (auto& node : list_head_iterator<Node>(my_list)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v2)
{
  std::array nodes {
    Node {1, "First node", {}},
    Node {2, "Second node", {}},
    Node {3, "Third node", {}},
  };

  list_head my_list { &my_list, &my_list };
  for (auto &n : nodes) add_tail(&my_list, &n.list);

  auto arr_iter = nodes.begin();

  using gh4ck3r::c_compat::v2::list_head_iterator;
  for (auto& node : list_head_iterator<&Node::list>(my_list)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, make_list_head)
{
  std::array nodes {
    Node {1, "First node", {}},
    Node {2, "Second node", {}},
    Node {3, "Third node", {}},
  };

  auto head = gh4ck3r::c_compat::make_list_head<&Node::list>(nodes);

  auto arr_iter = nodes.begin();

  using gh4ck3r::c_compat::v2::list_head_iterator;
  for (auto& node : list_head_iterator<&Node::list>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}
