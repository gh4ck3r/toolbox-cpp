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

  struct NestedNode {
    int id;
    std::string name;
    struct InnerNode {
      int subid;
      std::string alias;
      list_head list;
    } details;
  };

  auto make_nested_nodes() {
    std::array nodes {
      NestedNode {1, "First node",  {0x10, "one"}},
      NestedNode {2, "Second node", {0x20, "two"}},
      NestedNode {3, "Third node",  {0x40, "three"}},
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

  using gh4ck3r::c_compat::list_head::list_node_container;
  head << list_node_container(nodes);

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

  using gh4ck3r::c_compat::list_head::list_node_container;
  head << list_node_container<&CustomNode::link>(nodes);

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

  using gh4ck3r::c_compat::list_head::list_node_container;
  head  << list_node_container(nodes1)
        << list_node_container(nodes2);

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

  using namespace gh4ck3r::c_compat::list_head;
  list_head head {};
  head << list_node_container(nodes);

  auto arr_iter = nodes.begin();

  for (const auto& node : v1::list_head_iterator<Node>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v1_custom)
{
  auto nodes = make_custom_nodes();

  list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container<&CustomNode::link>(nodes);

  auto arr_iter = nodes.begin();

  for (const auto& node : v1::list_head_iterator<CustomNode, offsetof(CustomNode, link)>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v2)
{
  auto nodes = make_nodes();

  list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container(nodes);

  auto arr_iter = nodes.begin();

  for (const auto& node : v2::list_head_iterator<&Node::list>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v2_custom)
{
  auto nodes = make_custom_nodes();

  list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container<&CustomNode::link>(nodes);

  auto arr_iter = nodes.begin();

  using v2::list_head_iterator;
  for (const auto& node : list_head_iterator<&CustomNode::link>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v3)
{
  auto nodes = make_nodes();

  ::list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container(nodes);

  auto iter1 = v3::list_head_iterator<Node>(head);
  auto iter2 = v3::list_head_iterator<&Node::list>(head);
  EXPECT_EQ(iter1.begin(), iter2.begin());
  EXPECT_EQ(iter1.end(), iter2.end());

  auto arr_iter = nodes.begin();
  for (const auto& node : iter1) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }

  arr_iter = nodes.begin();
  for (const auto& node : iter2) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, iterator_v3_custom)
{
  auto nodes = make_custom_nodes();

  ::list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container<&CustomNode::link>(nodes);

  auto arr_iter = nodes.begin();
  for (const auto& node : v3::list_head_iterator<&CustomNode::link>(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, nested_node_by_offset)
{
  auto nodes = make_nested_nodes();

  ::list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container<&NestedNode::details, &NestedNode::InnerNode::list>(nodes);

  constexpr auto NestedNodeList = v3::list_head_iterator<
      NestedNode, offsetof(NestedNode, details.list)>;
  auto arr_iter = nodes.begin();
  for (const auto& node : NestedNodeList(head)) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, nested_node)
{
  auto nodes = make_nested_nodes();

  ::list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container<&NestedNode::details, &NestedNode::InnerNode::list>(nodes);

  auto arr_iter = nodes.begin();
  using NestedNodeList = list_view<&NestedNode::details,
                                   &NestedNode::InnerNode::list>;
  for (const auto &node : NestedNodeList{head}) {
    EXPECT_EQ(&(*arr_iter++), &node);
  }
}

TEST_F(list_head_test, list_node_iterator)
{
  auto nodes = make_nodes();

  list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container(nodes);

  auto arr_iter = nodes.begin();
  for (list_node_iterator<&Node::list> node {head}, end {}; node != end; ++node)
    EXPECT_EQ(&(*arr_iter++), &*node);
}

TEST_F(list_head_test, list_node_view)
{
  auto nodes = make_nodes();

  list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container(nodes);

  auto arr_iter = nodes.begin();
  using node_list_view = list_view<&Node::list>;
  for (const auto &node : node_list_view{head})
    EXPECT_EQ(&(*arr_iter++), &node);
}

TEST_F(list_head_test, empty_list_head)
{
  list_head head {};
  using namespace gh4ck3r::c_compat::list_head;

  auto view = list_view<&Node::list>{head};
  EXPECT_EQ(view.begin(), view.end());

  for ([[maybe_unused]] const auto &_ : view) FAIL();
}

TEST_F(list_head_test, list_node_iterator_post_inc)
{
  auto nodes = make_nodes();

  list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container(nodes);

  auto arr_iter = nodes.begin();
  list_node_iterator<&Node::list> list_iter {head}, end {};

  while (list_iter != end) {
    EXPECT_EQ(&(*arr_iter++), &(*list_iter++));
  }
}

TEST_F(list_head_test, large_node)
{
  struct LargeNode {
    char payload[65536];
    int id;
    list_head link;
  };

  std::vector<LargeNode> nodes(2);
  nodes[0].id = 100;
  nodes[1].id = 200;

  list_head head {};
  using namespace gh4ck3r::c_compat::list_head;
  head << list_node_container<&LargeNode::link>(nodes);

  size_t idx = 0;
  using LargeNodeView = list_view<&LargeNode::link>;
  for (const auto &node : LargeNodeView{head}) {
    EXPECT_EQ(nodes[idx].id, node.id);
    EXPECT_EQ(&nodes[idx], &node);
    idx++;
  }
  EXPECT_EQ(2u, idx);
}

