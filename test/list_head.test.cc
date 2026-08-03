#include "gh4ck3r/list_head.hh"
#include <gtest/gtest.h>

struct list_head_test : ::testing::Test {
 protected:
  struct Node {
      int id;
      std::string name;
      list_head list;
  };

  auto make_nodes() {
    std::array nodes {
      Node {1, "First node", {}},
      Node {2, "Second node", {}},
      Node {3, "Third node", {}},
    };
    return nodes;
  }

  struct CustomNode {
      int id;
      std::string name;
      // name of a list_head variable is differ from "list"
      list_head link;
  };

  auto make_custom_nodes() {
    std::array nodes {
      CustomNode {1, "First node", {}},
      CustomNode {2, "Second node", {}},
      CustomNode {3, "Third node", {}},
    };
    return nodes;
  }
};

TEST_F(list_head_test, list_node)
{
  auto nodes = make_nodes();

  list_head head {};
  ASSERT_FALSE(head.prev);
  ASSERT_FALSE(head.next);

  using gh4ck3r::c_compat::list_node;
  for (auto &n : nodes) head << list_node(n);

  ASSERT_EQ(head.prev->next, &head);
  ASSERT_EQ(head.next->prev, &head);

  auto node = head.next;
  for (auto i = 0u; i < nodes.size(); ++i, node = node->next) {
    EXPECT_EQ(node, &nodes[i].list);
  }
  EXPECT_EQ(node, &head);
}

TEST_F(list_head_test, list_custom_node)
{
  auto nodes = make_custom_nodes();

  list_head head {};
  ASSERT_FALSE(head.prev);
  ASSERT_FALSE(head.next);

  using gh4ck3r::c_compat::list_node;
  for (auto &n : nodes) head << list_node<&CustomNode::link>(n);

  ASSERT_EQ(head.prev->next, &head);
  ASSERT_EQ(head.next->prev, &head);

  auto node = head.next;
  for (auto i = 0u; i < nodes.size(); ++i, node = node->next) {
    EXPECT_EQ(node, &nodes[i].link);
  }
  EXPECT_EQ(node, &head);
}

TEST_F(list_head_test, list_container)
{
  auto nodes1 = make_nodes();
  std::array nodes2 {
    Node {4, "Forth node", {}},
    Node {5, "Fifth node", {}},
    Node {6, "Sixth node", {}},
  };

  list_head head {};
  ASSERT_FALSE(head.prev);
  ASSERT_FALSE(head.next);

  using gh4ck3r::c_compat::list_node;
  head  << list_node(nodes1)
        << list_node(nodes2);

  ASSERT_EQ(head.prev->next, &head);
  ASSERT_EQ(head.next->prev, &head);

  auto node = head.next;
  for (auto i = 0u; i < nodes1.size(); ++i, node = node->next) {
    EXPECT_EQ(node, &nodes1[i].list);
  }
  for (auto i = 0u; i < nodes2.size(); ++i, node = node->next) {
    EXPECT_EQ(node, &nodes2[i].list);
  }
  EXPECT_EQ(node, &head);
}

TEST_F(list_head_test, iterator_v1)
{
  auto nodes = make_nodes();

  list_head head {};
  using gh4ck3r::c_compat::list_node;
  head << list_node(nodes);

  auto arr_iter = nodes.begin();

  using gh4ck3r::c_compat::v1::list_head_iterator;
  for (auto& node : list_head_iterator<Node>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v1_custom)
{
  auto nodes = make_custom_nodes();

  list_head head {};
  using gh4ck3r::c_compat::list_node;
  head << list_node<&CustomNode::link>(nodes);

  auto arr_iter = nodes.begin();

  using gh4ck3r::c_compat::v1::list_head_iterator;
  for (auto& node : list_head_iterator<CustomNode, offsetof(CustomNode, link)>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v2)
{
  auto nodes = make_nodes();

  list_head head {};
  using gh4ck3r::c_compat::list_node;
  head << list_node(nodes);

  auto arr_iter = nodes.begin();

  using gh4ck3r::c_compat::v2::list_head_iterator;
  for (auto& node : list_head_iterator<&Node::list>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v2_custom)
{
  auto nodes = make_custom_nodes();

  list_head head {};
  using gh4ck3r::c_compat::list_node;
  head << list_node<&CustomNode::link>(nodes);

  auto arr_iter = nodes.begin();

  using gh4ck3r::c_compat::v2::list_head_iterator;
  for (auto& node : list_head_iterator<&CustomNode::link>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v3)
{
  auto nodes = make_nodes();

  list_head head {};
  using gh4ck3r::c_compat::list_node;
  head << list_node(nodes);

  using gh4ck3r::c_compat::v3::list_head_iterator;
  auto iter1 = list_head_iterator<Node>(head);
  auto iter2 = list_head_iterator<&Node::list>(head);
  EXPECT_EQ(iter1.begin(), iter2.begin());
  EXPECT_EQ(iter1.end(), iter2.end());

  auto arr_iter = nodes.begin();
  for (auto& node : iter1) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }

  arr_iter = nodes.begin();
  for (auto& node : iter2) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }

}
TEST_F(list_head_test, iterator_v3_custom)
{
  auto nodes = make_custom_nodes();

  list_head head {};
  using gh4ck3r::c_compat::list_node;
  head << list_node<&CustomNode::link>(nodes);

  using gh4ck3r::c_compat::v3::list_head_iterator;

  auto arr_iter = nodes.begin();
  for (auto& node : list_head_iterator<&CustomNode::link>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}
