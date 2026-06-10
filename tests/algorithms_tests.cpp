#include <talus/detail/algorithms.hpp>
#include <talus/detail/node.hpp>

#include <algorithm>
#include "test_check.hpp"
#include <cstddef>
#include <vector>
#include <string>

namespace {

using Box = talus::BoundingBox<double>;

struct Payload {
    int id = 0;
    std::string label;
};

struct MoveOnlyValue {
    int id = 0;
    explicit MoveOnlyValue(int i) : id(i) {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) = default;
    MoveOnlyValue& operator=(MoveOnlyValue&&) = default;
};

// Test: test_overlap_enlargement_no_siblings
// Verifies overlap enlargement is zero when a parent has no other children.
void test_overlap_enlargement_no_siblings() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child;
    child.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    parent.append_child(child.bounds(), &child);

    double delta = talus::detail::overlap_enlargement(parent, 0, Box{{0.0, 0.0}, {5.0, 5.0}});
    TALUS_CHECK(delta == 0.0);
}

// Test: test_overlap_enlargement_no_new_overlap
// Verifies expansion that remains disjoint reports no overlap growth.
void test_overlap_enlargement_no_new_overlap() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child_a, child_b;
    child_a.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    child_b.append_value(Box{{10.0, 10.0}, {11.0, 11.0}}, 2);
    parent.append_child(child_a.bounds(), &child_a);
    parent.append_child(child_b.bounds(), &child_b);

    // Expanding child_a to {0,0}-{3,3} still does not reach child_b.
    double delta = talus::detail::overlap_enlargement(parent, 0, Box{{0.0, 0.0}, {3.0, 3.0}});
    TALUS_CHECK(delta == 0.0);
}

// Test: test_overlap_enlargement_creates_new_overlap
// Verifies expansion into a sibling reports the newly created overlap area.
void test_overlap_enlargement_creates_new_overlap() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child_a, child_b;
    child_a.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    child_b.append_value(Box{{2.0, 0.0}, {3.0, 1.0}}, 2);
    parent.append_child(child_a.bounds(), &child_a);
    parent.append_child(child_b.bounds(), &child_b);

    // No current overlap. Expanding child_a to {0,0}-{2.5,1} creates 0.5×1=0.5 overlap.
    double delta = talus::detail::overlap_enlargement(parent, 0, Box{{0.0, 0.0}, {2.5, 1.0}});
    TALUS_CHECK(delta == 0.5);
}

// Test: test_overlap_enlargement_increases_existing_overlap
// Verifies expansion of an already-overlapping child reports only overlap delta.
void test_overlap_enlargement_increases_existing_overlap() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child_a, child_b;
    child_a.append_value(Box{{0.0, 0.0}, {2.0, 2.0}}, 1);
    child_b.append_value(Box{{1.0, 0.0}, {3.0, 2.0}}, 2);
    parent.append_child(child_a.bounds(), &child_a);
    parent.append_child(child_b.bounds(), &child_b);

    // Current overlap: {1,0}-{2,2} = 1×2 = 2.
    // After expanding child_a to {0,0}-{3,2}: overlap is {1,0}-{3,2} = 2×2 = 4. Delta = 2.
    double delta = talus::detail::overlap_enlargement(parent, 0, Box{{0.0, 0.0}, {3.0, 2.0}});
    TALUS_CHECK(delta == 2.0);
}

// Test: test_overlap_enlargement_sums_multiple_siblings
// Verifies overlap enlargement accumulates growth across all siblings.
void test_overlap_enlargement_sums_multiple_siblings() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child_a, child_b, child_c;
    child_a.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    child_b.append_value(Box{{2.0, 0.0}, {3.0, 1.0}}, 2);
    child_c.append_value(Box{{0.0, 2.0}, {1.0, 3.0}}, 3);
    parent.append_child(child_a.bounds(), &child_a);
    parent.append_child(child_b.bounds(), &child_b);
    parent.append_child(child_c.bounds(), &child_c);

    // Expanding child_a to {0,0}-{2.5,2.5}:
    //   overlap with child_b: {2,0}-{2.5,1} = 0.5×1 = 0.5 (was 0)
    //   overlap with child_c: {0,2}-{1,2.5} = 1×0.5 = 0.5 (was 0)
    //   total delta = 1.0
    double delta = talus::detail::overlap_enlargement(parent, 0, Box{{0.0, 0.0}, {2.5, 2.5}});
    TALUS_CHECK(delta == 1.0);
}

// Test: test_choose_leaf_selects_minimum_enlargement
// Verifies ChooseLeaf selects the child with minimum area enlargement.
void test_choose_leaf_selects_minimum_enlargement() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node left;
    Node right;

    left.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    right.append_value(Box{{10.0, 10.0}, {11.0, 11.0}}, 2);

    root.append_child(left.bounds(), &left);
    root.append_child(right.bounds(), &right);

    Node* chosen = talus::detail::choose_leaf(root, Box{{10.5, 10.5}, {10.5, 10.5}});

    TALUS_CHECK(chosen == &right);
}

// Test: test_choose_leaf_tie_breaks_by_smaller_area
// Verifies ChooseLeaf breaks equal enlargement ties by smaller current area.
void test_choose_leaf_tie_breaks_by_smaller_area() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node large;
    Node small;

    large.append_value(Box{{0.0, 0.0}, {10.0, 10.0}}, 1);
    small.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 2);

    root.append_child(large.bounds(), &large);
    root.append_child(small.bounds(), &small);

    Node* chosen = talus::detail::choose_leaf(root, Box{{2.5, 2.5}, {2.5, 2.5}});

    TALUS_CHECK(chosen == &small);
}

// Test: test_choose_leaf_tie_breaks_by_fewer_entries
// Verifies ChooseLeaf breaks equal area ties by fewer child entries.
void test_choose_leaf_tie_breaks_by_fewer_entries() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node crowded;
    Node sparse;

    crowded.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    crowded.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 2);
    sparse.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 3);

    root.append_child(crowded.bounds(), &crowded);
    root.append_child(sparse.bounds(), &sparse);

    Node* chosen = talus::detail::choose_leaf(root, Box{{0.5, 0.5}, {0.5, 0.5}});

    TALUS_CHECK(chosen == &sparse);
}

// Test: test_choose_leaf_prefers_overlap_enlargement_for_leaf_children
// Verifies R*-tree leaf-child selection prefers lower overlap enlargement.
void test_choose_leaf_prefers_overlap_enlargement_for_leaf_children() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node area_preferred_by_plain_rtree;
    Node existing_neighbor;
    Node overlap_preferred_by_rstar;

    area_preferred_by_plain_rtree.append_value(Box{{0.0, 0.0}, {100.0, 1.0}}, 1);
    existing_neighbor.append_value(Box{{-100.0, -100.0}, {25.0, 1.05}}, 2);
    overlap_preferred_by_rstar.append_value(Box{{50.0, -100.0}, {51.0, -99.0}}, 3);

    root.append_child(area_preferred_by_plain_rtree.bounds(), &area_preferred_by_plain_rtree);
    root.append_child(existing_neighbor.bounds(), &existing_neighbor);
    root.append_child(overlap_preferred_by_rstar.bounds(), &overlap_preferred_by_rstar);

    Node* chosen = talus::detail::choose_leaf(root, Box{{50.0, 1.2}, {50.0, 1.2}});

    TALUS_CHECK(chosen == &overlap_preferred_by_rstar);
}

// Test: test_insert_appends_to_root_leaf
// Verifies non-splitting insert appends directly to a leaf root.
void test_insert_appends_to_root_leaf() {
    using Node = talus::detail::RTreeNode<Payload, double, 4>;

    Node root;

    auto result = talus::detail::insert(root, Box{{1.0, 2.0}, {1.0, 2.0}}, Payload{7, "root"});

    TALUS_CHECK(result.inserted);
    TALUS_CHECK(!result.needs_split());
    TALUS_CHECK(result.leaf == &root);
    TALUS_CHECK(root.count() == 1);
    TALUS_CHECK(root.value_at(0).value().id == 7);
    TALUS_CHECK(root.value_at(0).value().label == "root");
    TALUS_CHECK((root.bounds() == Box{{1.0, 2.0}, {1.0, 2.0}}));
}

// Test: test_insert_routes_to_child_and_refreshes_ancestor_bounds
// Verifies insert routes through ChooseLeaf and refreshes parent child bounds.
void test_insert_routes_to_child_and_refreshes_ancestor_bounds() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node left;
    Node right;

    left.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    right.append_value(Box{{10.0, 10.0}, {11.0, 11.0}}, 2);

    root.append_child(left.bounds(), &left);
    root.append_child(right.bounds(), &right);

    auto result = talus::detail::insert(root, Box{{12.0, 12.0}, {13.0, 13.0}}, 3);

    TALUS_CHECK(result.inserted);
    TALUS_CHECK(result.leaf == &right);
    TALUS_CHECK(right.count() == 2);
    TALUS_CHECK(right.value_at(1).value() == 3);
    TALUS_CHECK((right.bounds() == Box{{10.0, 10.0}, {13.0, 13.0}}));
    TALUS_CHECK((root.child_at(1).bounds == right.bounds()));
    TALUS_CHECK((root.bounds() == Box{{0.0, 0.0}, {13.0, 13.0}}));
}

// Test: test_insert_reports_overflow_without_splitting
// Verifies insert can occupy the overflow slot and report split is required.
void test_insert_reports_overflow_without_splitting() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root;
    root.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    root.append_value(Box{{1.0, 1.0}, {1.0, 1.0}}, 1);
    root.append_value(Box{{2.0, 2.0}, {2.0, 2.0}}, 2);
    root.append_value(Box{{3.0, 3.0}, {3.0, 3.0}}, 3);

    auto result = talus::detail::insert(root, Box{{4.0, 4.0}, {4.0, 4.0}}, 4);

    TALUS_CHECK(result.inserted);
    TALUS_CHECK(result.needs_split());
    TALUS_CHECK(result.overflow == &root);
    TALUS_CHECK(root.has_overflow());
    TALUS_CHECK(root.count() == Node::entry_capacity);
}

// Test: test_insert_refreshes_bounds_three_levels_deep
// Verifies insert propagates refreshed bounds through multiple ancestor levels.
void test_insert_refreshes_bounds_three_levels_deep() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node mid(false);
    Node leaf;

    leaf.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    mid.append_child(leaf.bounds(), &leaf);
    root.append_child(mid.bounds(), &mid);

    // Insert expands leaf bounds, which must propagate through mid and then root.
    auto result = talus::detail::insert(root, Box{{5.0, 5.0}, {6.0, 6.0}}, 2);

    TALUS_CHECK(result.inserted);
    TALUS_CHECK(result.leaf == &leaf);

    const Box expected{{0.0, 0.0}, {6.0, 6.0}};
    TALUS_CHECK((leaf.bounds() == expected));
    TALUS_CHECK((mid.child_at(0).bounds == expected));   // mid's stored child bounds updated
    TALUS_CHECK((root.child_at(0).bounds == expected));  // root's stored child bounds updated
}

// Test: test_choose_leaf_returns_leaf_root_directly
// Verifies ChooseLeaf returns a leaf root without descending.
void test_choose_leaf_returns_leaf_root_directly() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root; // leaf by default
    root.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);

    Node* chosen = talus::detail::choose_leaf(root, Box{{5.0, 5.0}, {5.0, 5.0}});

    TALUS_CHECK(chosen == &root);
}

// Test: test_choose_leaf_descends_through_internal_nodes
// Verifies ChooseLeaf descends through internal levels to the best target leaf.
void test_choose_leaf_descends_through_internal_nodes() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node branch_near(false);
    Node branch_far(false);
    Node leaf_near;
    Node leaf_far;

    leaf_near.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    leaf_far.append_value(Box{{10.0, 10.0}, {11.0, 11.0}}, 2);

    branch_near.append_child(leaf_near.bounds(), &leaf_near);
    branch_far.append_child(leaf_far.bounds(), &leaf_far);

    root.append_child(branch_near.bounds(), &branch_near);
    root.append_child(branch_far.bounds(), &branch_far);

    // (10.5, 10.5) requires zero enlargement in branch_far at both levels.
    Node* chosen = talus::detail::choose_leaf(root, Box{{10.5, 10.5}, {10.5, 10.5}});

    TALUS_CHECK(chosen == &leaf_far);
}

// Test: test_choose_leaf_uses_area_enlargement_at_internal_level
// Verifies internal-level ChooseLeaf uses area enlargement rather than overlap enlargement.
void test_choose_leaf_uses_area_enlargement_at_internal_level() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node branch_a(false); // large bounding box that already contains the target
    Node branch_b(false); // small bounding box far from the target
    Node leaf_a;
    Node leaf_b;

    leaf_a.append_value(Box{{0.0, 0.0}, {100.0, 100.0}}, 1);
    leaf_b.append_value(Box{{200.0, 200.0}, {201.0, 201.0}}, 2);

    branch_a.append_child(leaf_a.bounds(), &leaf_a);
    branch_b.append_child(leaf_b.bounds(), &leaf_b);

    root.append_child(branch_a.bounds(), &branch_a);
    root.append_child(branch_b.bounds(), &branch_b);

    // (50, 50) is already inside branch_a (zero area growth) so it wins despite
    // branch_b having zero overlap with branch_a at this level.
    Node* chosen = talus::detail::choose_leaf(root, Box{{50.0, 50.0}, {50.0, 50.0}});

    TALUS_CHECK(chosen == &leaf_a);
}

bool leaf_contains_value(const talus::detail::RTreeNode<int, double, 4>& node, int value) {
    for (const auto& entry : node.values()) {
        if (entry.value() == value) {
            return true;
        }
    }
    return false;
}

bool internal_contains_child(
    const talus::detail::RTreeNode<int, double, 4>& node,
    const talus::detail::RTreeNode<int, double, 4>* child) {
    for (const auto& entry : node.children()) {
        if (entry.child == child) {
            return true;
        }
    }
    return false;
}

// Test: test_split_node_redistributes_leaf_entries
// Verifies leaf splitting preserves all values and separates clustered bounds.
void test_split_node_redistributes_leaf_entries() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    Node sibling;

    node.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    node.append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, 1);
    node.append_value(Box{{2.0, 0.0}, {2.0, 0.0}}, 2);
    node.append_value(Box{{100.0, 0.0}, {100.0, 0.0}}, 100);
    node.append_value(Box{{101.0, 0.0}, {101.0, 0.0}}, 101);

    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK(result.left == &node);
    TALUS_CHECK(result.right == &sibling);
    TALUS_CHECK(node.is_leaf());
    TALUS_CHECK(sibling.is_leaf());
    TALUS_CHECK(!node.has_overflow());
    TALUS_CHECK(!sibling.has_overflow());
    TALUS_CHECK(!node.underfull());
    TALUS_CHECK(!sibling.underfull());
    TALUS_CHECK(node.count() + sibling.count() == Node::entry_capacity);

    for (int value : {0, 1, 2, 100, 101}) {
        TALUS_CHECK(leaf_contains_value(node, value) || leaf_contains_value(sibling, value));
    }

    TALUS_CHECK((node.bounds().max.x <= 2.0 && sibling.bounds().min.x >= 100.0)
        || (sibling.bounds().max.x <= 2.0 && node.bounds().min.x >= 100.0));
}

// Test: test_split_node_redistributes_internal_entries_and_updates_parents
// Verifies internal splitting preserves children and updates child parent pointers.
void test_split_node_redistributes_internal_entries_and_updates_parents() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node node(false);
    Node sibling;
    Node child0;
    Node child1;
    Node child2;
    Node child100;
    Node child101;

    child0.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    child1.append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, 1);
    child2.append_value(Box{{2.0, 0.0}, {2.0, 0.0}}, 2);
    child100.append_value(Box{{100.0, 0.0}, {100.0, 0.0}}, 100);
    child101.append_value(Box{{101.0, 0.0}, {101.0, 0.0}}, 101);

    node.append_child(child0.bounds(), &child0);
    node.append_child(child1.bounds(), &child1);
    node.append_child(child2.bounds(), &child2);
    node.append_child(child100.bounds(), &child100);
    node.append_child(child101.bounds(), &child101);
    parent.append_child(node.bounds(), &node);

    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK(result.left == &node);
    TALUS_CHECK(result.right == &sibling);
    TALUS_CHECK(node.is_internal());
    TALUS_CHECK(sibling.is_internal());
    TALUS_CHECK(node.parent() == &parent);
    TALUS_CHECK(sibling.parent() == &parent);
    TALUS_CHECK(!node.has_overflow());
    TALUS_CHECK(!sibling.has_overflow());
    TALUS_CHECK(!node.underfull());
    TALUS_CHECK(!sibling.underfull());
    TALUS_CHECK(node.count() + sibling.count() == Node::entry_capacity);

    for (Node* child : {&child0, &child1, &child2, &child100, &child101}) {
        TALUS_CHECK(internal_contains_child(node, child) || internal_contains_child(sibling, child));
        TALUS_CHECK(child->parent() == &node || child->parent() == &sibling);
    }

    TALUS_CHECK((node.bounds().max.x <= 2.0 && sibling.bounds().min.x >= 100.0)
        || (sibling.bounds().max.x <= 2.0 && node.bounds().min.x >= 100.0));
}

// Test: test_split_node_resets_internal_sibling_for_leaf_split
// Verifies a leaf split resets and clears a sibling that was previously internal.
void test_split_node_resets_internal_sibling_for_leaf_split() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    Node sibling(false);
    Node stale_child;

    stale_child.append_value(Box{{-10.0, -10.0}, {-9.0, -9.0}}, -1);
    sibling.append_child(stale_child.bounds(), &stale_child);
    TALUS_CHECK(stale_child.parent() == &sibling);

    node.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    node.append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, 1);
    node.append_value(Box{{2.0, 0.0}, {2.0, 0.0}}, 2);
    node.append_value(Box{{100.0, 0.0}, {100.0, 0.0}}, 100);
    node.append_value(Box{{101.0, 0.0}, {101.0, 0.0}}, 101);

    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK(sibling.is_leaf());
    TALUS_CHECK(stale_child.parent() == nullptr);
    TALUS_CHECK(node.count() + sibling.count() == Node::entry_capacity);
    TALUS_CHECK(!leaf_contains_value(node, -1));
    TALUS_CHECK(!leaf_contains_value(sibling, -1));
}

// Test: test_split_node_resets_leaf_sibling_for_internal_split
// Verifies an internal split resets and clears a sibling that was previously a leaf.
void test_split_node_resets_leaf_sibling_for_internal_split() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node(false);
    Node sibling;
    Node child0;
    Node child1;
    Node child2;
    Node child100;
    Node child101;

    sibling.append_value(Box{{-10.0, -10.0}, {-9.0, -9.0}}, -1);

    child0.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    child1.append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, 1);
    child2.append_value(Box{{2.0, 0.0}, {2.0, 0.0}}, 2);
    child100.append_value(Box{{100.0, 0.0}, {100.0, 0.0}}, 100);
    child101.append_value(Box{{101.0, 0.0}, {101.0, 0.0}}, 101);

    node.append_child(child0.bounds(), &child0);
    node.append_child(child1.bounds(), &child1);
    node.append_child(child2.bounds(), &child2);
    node.append_child(child100.bounds(), &child100);
    node.append_child(child101.bounds(), &child101);

    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK(sibling.is_internal());
    TALUS_CHECK(node.count() + sibling.count() == Node::entry_capacity);
    for (Node* child : {&child0, &child1, &child2, &child100, &child101}) {
        TALUS_CHECK(internal_contains_child(node, child) || internal_contains_child(sibling, child));
        TALUS_CHECK(child->parent() == &node || child->parent() == &sibling);
    }
}

// Test: test_split_node_can_choose_y_axis_distribution
// Verifies SplitNode can choose a vertical distribution when y-axis separation is best.
void test_split_node_can_choose_y_axis_distribution() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    Node sibling;

    node.append_value(Box{{0.0, 0.0}, {1.0, 0.0}}, 0);
    node.append_value(Box{{0.0, 1.0}, {1.0, 1.0}}, 1);
    node.append_value(Box{{0.0, 2.0}, {1.0, 2.0}}, 2);
    node.append_value(Box{{0.0, 100.0}, {1.0, 100.0}}, 100);
    node.append_value(Box{{0.0, 101.0}, {1.0, 101.0}}, 101);

    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK(!node.has_overflow());
    TALUS_CHECK(!sibling.has_overflow());
    TALUS_CHECK(!node.underfull());
    TALUS_CHECK(!sibling.underfull());
    TALUS_CHECK(node.count() + sibling.count() == Node::entry_capacity);
    TALUS_CHECK((node.bounds().max.y <= 2.0 && sibling.bounds().min.y >= 100.0)
        || (sibling.bounds().max.y <= 2.0 && node.bounds().min.y >= 100.0));
}

// Test: test_split_node_uses_deterministic_order_for_identical_bounds
// Verifies identical bounds split deterministically using insertion-order fallback.
void test_split_node_uses_deterministic_order_for_identical_bounds() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node first;
    Node first_sibling;
    Node second;
    Node second_sibling;
    const Box same_bounds{{0.0, 0.0}, {1.0, 1.0}};

    for (int value : {0, 1, 2, 3, 4}) {
        first.append_value(same_bounds, value);
        second.append_value(same_bounds, value);
    }

    auto first_result = talus::detail::split_node(first, first_sibling);
    auto second_result = talus::detail::split_node(second, second_sibling);

    TALUS_CHECK(first_result.split);
    TALUS_CHECK(second_result.split);
    TALUS_CHECK(first.count() == second.count());
    TALUS_CHECK(first_sibling.count() == second_sibling.count());
    for (std::size_t i = 0; i < first.count(); ++i) {
        TALUS_CHECK(first.value_at(i).value() == second.value_at(i).value());
    }
    for (std::size_t i = 0; i < first_sibling.count(); ++i) {
        TALUS_CHECK(first_sibling.value_at(i).value() == second_sibling.value_at(i).value());
    }
}

// Test: test_split_node_evaluates_multiple_distributions_for_larger_capacity
// Verifies larger node capacities evaluate multiple valid split distributions.
void test_split_node_evaluates_multiple_distributions_for_larger_capacity() {
    using Node = talus::detail::RTreeNode<int, double, 6>;

    Node node;
    Node sibling;

    node.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    node.append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, 1);
    node.append_value(Box{{2.0, 0.0}, {2.0, 0.0}}, 2);
    node.append_value(Box{{50.0, 0.0}, {50.0, 0.0}}, 50);
    node.append_value(Box{{100.0, 0.0}, {100.0, 0.0}}, 100);
    node.append_value(Box{{101.0, 0.0}, {101.0, 0.0}}, 101);
    node.append_value(Box{{102.0, 0.0}, {102.0, 0.0}}, 102);

    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK(!node.has_overflow());
    TALUS_CHECK(!sibling.has_overflow());
    TALUS_CHECK(!node.underfull());
    TALUS_CHECK(!sibling.underfull());
    TALUS_CHECK(node.count() + sibling.count() == Node::entry_capacity);
    TALUS_CHECK((node.bounds().max.x <= 50.0 && sibling.bounds().min.x >= 100.0)
        || (sibling.bounds().max.x <= 50.0 && node.bounds().min.x >= 100.0));
}

// Test: test_split_node_leaf_bounds_cover_original
// Verifies leaf split output bounds combine to the original aggregate bounds.
void test_split_node_leaf_bounds_cover_original() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    Node sibling;

    const Box b0{{0.0, 5.0}, {1.0, 6.0}};
    const Box b1{{3.0, 0.0}, {4.0, 1.0}};
    const Box b2{{7.0, 8.0}, {8.0, 9.0}};
    const Box b3{{-2.0, -1.0}, {-1.0, 0.0}};
    const Box b4{{10.0, 10.0}, {11.0, 11.0}};
    const Box original = b0.expand(b1).expand(b2).expand(b3).expand(b4);

    node.append_value(b0, 0);
    node.append_value(b1, 1);
    node.append_value(b2, 2);
    node.append_value(b3, 3);
    node.append_value(b4, 4);

    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK((node.bounds().expand(sibling.bounds()) == original));
}

// Test: test_split_node_internal_bounds_cover_original
// Verifies internal split output bounds combine to the original aggregate bounds.
void test_split_node_internal_bounds_cover_original() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node(false);
    Node sibling;
    Node child0;
    Node child1;
    Node child2;
    Node child3;
    Node child4;

    const Box b0{{0.0, 5.0}, {1.0, 6.0}};
    const Box b1{{3.0, 0.0}, {4.0, 1.0}};
    const Box b2{{7.0, 8.0}, {8.0, 9.0}};
    const Box b3{{-2.0, -1.0}, {-1.0, 0.0}};
    const Box b4{{10.0, 10.0}, {11.0, 11.0}};
    const Box original = b0.expand(b1).expand(b2).expand(b3).expand(b4);

    child0.append_value(b0, 0);
    child1.append_value(b1, 1);
    child2.append_value(b2, 2);
    child3.append_value(b3, 3);
    child4.append_value(b4, 4);

    node.append_child(child0.bounds(), &child0);
    node.append_child(child1.bounds(), &child1);
    node.append_child(child2.bounds(), &child2);
    node.append_child(child3.bounds(), &child3);
    node.append_child(child4.bounds(), &child4);

    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK(node.is_internal());
    TALUS_CHECK(sibling.is_internal());
    TALUS_CHECK((node.bounds().expand(sibling.bounds()) == original));
}

// Test: test_split_node_leaf_move_only_values
// Verifies leaf splitting works with move-only value types.
void test_split_node_leaf_move_only_values() {
    using Node = talus::detail::RTreeNode<MoveOnlyValue, double, 4>;

    Node node;
    Node sibling;

    for (int i = 0; i < static_cast<int>(Node::entry_capacity); ++i) {
        node.append_value(
            Box{{static_cast<double>(i), 0.0}, {static_cast<double>(i + 1), 1.0}},
            MoveOnlyValue{i});
    }

    TALUS_CHECK(node.has_overflow());
    auto result = talus::detail::split_node(node, sibling);

    TALUS_CHECK(result.split);
    TALUS_CHECK(node.count() + sibling.count() == Node::entry_capacity);
}

void collect_leaf_values(
    const talus::detail::RTreeNode<int, double, 4>& node,
    std::vector<int>& values) {
    if (node.is_leaf()) {
        for (const auto& entry : node.values()) {
            values.push_back(entry.value());
        }
        return;
    }

    for (const auto& entry : node.children()) {
        collect_leaf_values(*entry.child, values);
    }
}

void assert_internal_bounds_match_children(const talus::detail::RTreeNode<int, double, 4>& node) {
    if (node.is_leaf()) {
        return;
    }

    Box combined = node.child_at(0).bounds;
    for (std::size_t i = 0; i < node.count(); ++i) {
        const auto& entry = node.child_at(i);
        TALUS_CHECK(entry.child != nullptr);
        TALUS_CHECK(entry.child->parent() == &node);
        TALUS_CHECK((entry.bounds == entry.child->bounds()));
        if (i > 0) {
            combined = combined.expand(entry.bounds);
        }
        assert_internal_bounds_match_children(*entry.child);
    }
    TALUS_CHECK((node.bounds() == combined));
}

// Test: test_adjust_tree_grows_leaf_root_in_place
// Verifies AdjustTree converts an overflowing leaf root into a stable internal root.
void test_adjust_tree_grows_leaf_root_in_place() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 8> pool;
    Node root;

    for (int value : {0, 1, 2, 100, 101}) {
        root.append_value(
            Box{{static_cast<double>(value), 0.0}, {static_cast<double>(value), 0.0}},
            value);
    }

    auto result = talus::detail::adjust_tree(root, &root, pool);

    TALUS_CHECK(result.adjusted);
    TALUS_CHECK(result.root == &root);
    TALUS_CHECK(result.split_count == 1);
    TALUS_CHECK(result.grew_height);
    TALUS_CHECK(root.is_internal());
    TALUS_CHECK(root.parent() == nullptr);
    TALUS_CHECK(root.count() == 2);
    TALUS_CHECK(!root.has_overflow());
    assert_internal_bounds_match_children(root);

    std::vector<int> values;
    collect_leaf_values(root, values);
    TALUS_CHECK(values.size() == Node::entry_capacity);
    for (int value : {0, 1, 2, 100, 101}) {
        TALUS_CHECK(std::find(values.begin(), values.end(), value) != values.end());
    }
}

// Test: test_adjust_tree_attaches_split_sibling_to_parent
// Verifies AdjustTree updates an overflowing child's parent bounds and sibling link.
void test_adjust_tree_attaches_split_sibling_to_parent() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node leaf;
    talus::detail::PoolAllocator<Node, 8> pool;
    Node root(false);

    for (int value : {0, 1, 2, 100, 101}) {
        leaf.append_value(
            Box{{static_cast<double>(value), 0.0}, {static_cast<double>(value), 0.0}},
            value);
    }
    root.append_child(leaf.bounds(), &leaf);

    auto result = talus::detail::adjust_tree(root, &leaf, pool);

    TALUS_CHECK(result.adjusted);
    TALUS_CHECK(result.split_count == 1);
    TALUS_CHECK(!result.grew_height);
    TALUS_CHECK(root.is_internal());
    TALUS_CHECK(root.count() == 2);
    TALUS_CHECK(leaf.parent() == &root);
    TALUS_CHECK(!leaf.has_overflow());
    assert_internal_bounds_match_children(root);

    std::vector<int> values;
    collect_leaf_values(root, values);
    TALUS_CHECK(values.size() == Node::entry_capacity);
    for (int value : {0, 1, 2, 100, 101}) {
        TALUS_CHECK(std::find(values.begin(), values.end(), value) != values.end());
    }
}

// Test: test_adjust_tree_propagates_parent_split_to_new_root
// Verifies AdjustTree cascades a child split through a full parent and grows the root.
void test_adjust_tree_propagates_parent_split_to_new_root() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node overflowing_leaf;
    Node filler0;
    Node filler1;
    Node filler2;
    talus::detail::PoolAllocator<Node, 16> pool;
    Node root(false);

    for (int value : {0, 1, 2, 100, 101}) {
        overflowing_leaf.append_value(
            Box{{static_cast<double>(value), 0.0}, {static_cast<double>(value), 0.0}},
            value);
    }

    filler0.append_value(Box{{200.0, 0.0}, {200.0, 0.0}}, 200);
    filler1.append_value(Box{{300.0, 0.0}, {300.0, 0.0}}, 300);
    filler2.append_value(Box{{400.0, 0.0}, {400.0, 0.0}}, 400);

    root.append_child(overflowing_leaf.bounds(), &overflowing_leaf);
    root.append_child(filler0.bounds(), &filler0);
    root.append_child(filler1.bounds(), &filler1);
    root.append_child(filler2.bounds(), &filler2);

    auto result = talus::detail::adjust_tree(root, &overflowing_leaf, pool);

    TALUS_CHECK(result.adjusted);
    TALUS_CHECK(result.split_count == 2);
    TALUS_CHECK(result.grew_height);
    TALUS_CHECK(root.is_internal());
    TALUS_CHECK(root.parent() == nullptr);
    TALUS_CHECK(root.count() == 2);
    TALUS_CHECK(!root.has_overflow());
    assert_internal_bounds_match_children(root);

    std::vector<int> values;
    collect_leaf_values(root, values);
    for (int value : {0, 1, 2, 100, 101, 200, 300, 400}) {
        TALUS_CHECK(std::find(values.begin(), values.end(), value) != values.end());
    }
}

// Test: test_insert_with_split_keeps_tree_valid_after_root_split
// Verifies split-aware insert appends the entry and fully adjusts an overflowing root.
void test_insert_with_split_keeps_tree_valid_after_root_split() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 8> pool;
    Node root;

    for (int value : {0, 1, 2, 3}) {
        root.append_value(
            Box{{static_cast<double>(value), 0.0}, {static_cast<double>(value), 0.0}},
            value);
    }

    auto result = talus::detail::insert_with_split(
        root,
        pool,
        Box{{4.0, 0.0}, {4.0, 0.0}},
        4);

    TALUS_CHECK(result.inserted);
    TALUS_CHECK(result.root == &root);
    TALUS_CHECK(result.split_count == 1);
    TALUS_CHECK(result.grew_height);
    TALUS_CHECK(root.is_internal());
    assert_internal_bounds_match_children(root);

    std::vector<int> values;
    collect_leaf_values(root, values);
    for (int value : {0, 1, 2, 3, 4}) {
        TALUS_CHECK(std::find(values.begin(), values.end(), value) != values.end());
    }
}

// Test: test_erase_leaf_root_removes_one_matching_entry
// Verifies Delete finds a matching leaf-root entry, removes only that entry,
// refreshes root bounds, and reports no subtree condensation.
void test_erase_leaf_root_removes_one_matching_entry() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 8> pool;
    Node root;
    root.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    root.append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, 1);
    root.append_value(Box{{2.0, 0.0}, {2.0, 0.0}}, 2);

    auto result = talus::detail::erase(
        root,
        pool,
        Box{{1.0, 0.0}, {1.0, 0.0}},
        [](const int& value) {
            return value == 1;
        });

    TALUS_CHECK(result.erased);
    TALUS_CHECK(result.condensed_nodes == 0);
    TALUS_CHECK(result.reinserted_entries == 0);
    TALUS_CHECK(root.is_leaf());
    TALUS_CHECK(root.count() == 2);
    TALUS_CHECK(!leaf_contains_value(root, 1));
    TALUS_CHECK(leaf_contains_value(root, 0));
    TALUS_CHECK(leaf_contains_value(root, 2));
    TALUS_CHECK((root.bounds() == Box{{0.0, 0.0}, {2.0, 0.0}}));
}

// Test: test_erase_returns_false_when_no_entry_matches
// Verifies Delete leaves the tree untouched when candidate bounds exist but
// the caller predicate rejects the stored value.
void test_erase_returns_false_when_no_entry_matches() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 8> pool;
    Node root;
    root.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    root.append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, 1);

    auto result = talus::detail::erase(
        root,
        pool,
        Box{{1.0, 0.0}, {1.0, 0.0}},
        [](const int& value) {
            return value == 99;
        });

    TALUS_CHECK(!result.erased);
    TALUS_CHECK(root.count() == 2);
    TALUS_CHECK(leaf_contains_value(root, 0));
    TALUS_CHECK(leaf_contains_value(root, 1));
}

// Test: test_erase_condenses_underfull_child_and_collapses_root
// Verifies Delete detaches an underfull non-root leaf, reinserts its remaining
// entries, destroys the detached subtree, and collapses a one-child root.
void test_erase_condenses_underfull_child_and_collapses_root() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 8> pool;
    Node root(false);
    Node* left = pool.create();
    Node* right = pool.create();

    left->append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    left->append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, 1);
    right->append_value(Box{{100.0, 0.0}, {100.0, 0.0}}, 100);
    right->append_value(Box{{101.0, 0.0}, {101.0, 0.0}}, 101);
    root.append_child(left->bounds(), left);
    root.append_child(right->bounds(), right);

    auto result = talus::detail::erase(
        root,
        pool,
        Box{{0.0, 0.0}, {0.0, 0.0}},
        [](const int& value) {
            return value == 0;
        });

    TALUS_CHECK(result.erased);
    TALUS_CHECK(result.condensed_nodes == 1);
    TALUS_CHECK(result.reinserted_entries == 1);
    TALUS_CHECK(root.is_leaf());
    TALUS_CHECK(root.parent() == nullptr);
    TALUS_CHECK(root.count() == 3);
    TALUS_CHECK(!leaf_contains_value(root, 0));
    for (int value : {1, 100, 101}) {
        TALUS_CHECK(leaf_contains_value(root, value));
    }
    TALUS_CHECK((root.bounds() == Box{{1.0, 0.0}, {101.0, 0.0}}));
    TALUS_CHECK(pool.empty());
}

// Test: test_erase_move_only_leaf_value
// Verifies Delete works for move-only values when the caller supplies a
// predicate, matching the relocation requirements used by split and condense.
void test_erase_move_only_leaf_value() {
    using Node = talus::detail::RTreeNode<MoveOnlyValue, double, 4>;

    talus::detail::PoolAllocator<Node, 8> pool;
    Node root;
    root.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, MoveOnlyValue{0});
    root.append_value(Box{{1.0, 0.0}, {1.0, 0.0}}, MoveOnlyValue{1});
    root.append_value(Box{{2.0, 0.0}, {2.0, 0.0}}, MoveOnlyValue{2});

    auto result = talus::detail::erase(
        root,
        pool,
        Box{{1.0, 0.0}, {1.0, 0.0}},
        [](const MoveOnlyValue& value) {
            return value.id == 1;
        });

    TALUS_CHECK(result.erased);
    TALUS_CHECK(root.count() == 2);
    for (const auto& entry : root.values()) {
        TALUS_CHECK(entry.value().id != 1);
    }
}

// Test: test_search_empty_root_returns_no_matches
// Verifies Search handles an empty tree without invoking the result visitor.
void test_search_empty_root_returns_no_matches() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root;
    std::vector<int> matches;

    const std::size_t count = talus::detail::search(
        root,
        Box{{0.0, 0.0}, {10.0, 10.0}},
        [&](const int& value) {
            matches.push_back(value);
        });

    TALUS_CHECK(count == 0);
    TALUS_CHECK(matches.empty());
}

// Test: test_search_leaf_reports_intersecting_values
// Verifies Search visits only leaf entries whose bounds intersect the query,
// including entries that touch the query boundary.
void test_search_leaf_reports_intersecting_values() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root;
    root.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 0);
    root.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 1);
    root.append_value(Box{{4.0, 4.0}, {5.0, 5.0}}, 2);

    std::vector<int> matches;
    const std::size_t count = talus::detail::search(
        root,
        Box{{1.0, 1.0}, {4.0, 4.0}},
        [&](const int& value) {
            matches.push_back(value);
        });

    std::sort(matches.begin(), matches.end());
    TALUS_CHECK(count == 3);
    TALUS_CHECK((matches == std::vector<int>{0, 1, 2}));
}

// Test: test_search_internal_prunes_disjoint_children
// Verifies Search descends only through child bounds that intersect the query.
void test_search_internal_prunes_disjoint_children() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node left;
    Node right;

    left.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 0);
    left.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 1);
    right.append_value(Box{{100.0, 100.0}, {101.0, 101.0}}, 2);

    root.append_child(left.bounds(), &left);
    root.append_child(right.bounds(), &right);

    std::vector<int> matches;
    const std::size_t count = talus::detail::search(
        root,
        Box{{0.5, 0.5}, {2.5, 2.5}},
        [&](const int& value) {
            matches.push_back(value);
        });

    std::sort(matches.begin(), matches.end());
    TALUS_CHECK(count == 2);
    TALUS_CHECK((matches == std::vector<int>{0, 1}));
}

// Test: test_search_split_insert_tree_matches_deterministic_query
// Verifies Search finds the expected values in a tree produced by split-aware
// insertion, including after root growth and child bound adjustment.
void test_search_split_insert_tree_matches_deterministic_query() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 16> pool;
    Node root;

    for (int value : {0, 1, 2, 3, 100, 101, 102, 103, 200}) {
        auto result = talus::detail::insert_with_split(
            root,
            pool,
            Box{
                {static_cast<double>(value), static_cast<double>(value)},
                {static_cast<double>(value), static_cast<double>(value)}
            },
            value);
        TALUS_CHECK(result.inserted);
    }

    TALUS_CHECK(root.is_internal());
    assert_internal_bounds_match_children(root);

    std::vector<int> matches;
    const std::size_t count = talus::detail::search(
        root,
        Box{{99.5, 99.5}, {102.5, 102.5}},
        [&](const int& value) {
            matches.push_back(value);
        });

    std::sort(matches.begin(), matches.end());
    TALUS_CHECK(count == 3);
    TALUS_CHECK((matches == std::vector<int>{100, 101, 102}));
}

// Test: test_radius_search_empty_root_returns_no_matches
// Verifies RadiusSearch handles an empty tree without invoking the result
// visitor.
void test_radius_search_empty_root_returns_no_matches() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root;
    std::vector<int> matches;

    const std::size_t count = talus::detail::radius_search(
        root,
        talus::Point<double>{0.0, 0.0},
        10.0,
        [&](const int& value) {
            matches.push_back(value);
        });

    TALUS_CHECK(count == 0);
    TALUS_CHECK(matches.empty());
}

// Test: test_radius_search_leaf_reports_values_within_radius
// Verifies RadiusSearch visits leaf entries whose stored bounds have minimum
// point-to-box distance inside the radius, including the exact boundary.
void test_radius_search_leaf_reports_values_within_radius() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root;
    root.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    root.append_value(Box{{3.0, 4.0}, {3.0, 4.0}}, 1);
    root.append_value(Box{{6.0, 8.0}, {6.0, 8.0}}, 2);
    root.append_value(Box{{-2.0, -1.0}, {-1.0, 1.0}}, 3);

    std::vector<int> matches;
    const std::size_t count = talus::detail::radius_search(
        root,
        talus::Point<double>{0.0, 0.0},
        5.0,
        [&](const int& value) {
            matches.push_back(value);
        });

    std::sort(matches.begin(), matches.end());
    TALUS_CHECK(count == 3);
    TALUS_CHECK((matches == std::vector<int>{0, 1, 3}));
}

// Test: test_radius_search_internal_prunes_distant_children
// Verifies RadiusSearch descends only through child bounds whose minimum
// distance to the query point is inside the requested radius.
void test_radius_search_internal_prunes_distant_children() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node left;
    Node right;

    left.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    left.append_value(Box{{3.0, 4.0}, {3.0, 4.0}}, 1);
    right.append_value(Box{{100.0, 100.0}, {101.0, 101.0}}, 2);

    root.append_child(left.bounds(), &left);
    root.append_child(right.bounds(), &right);

    std::vector<int> matches;
    const std::size_t count = talus::detail::radius_search(
        root,
        talus::Point<double>{0.0, 0.0},
        5.0,
        [&](const int& value) {
            matches.push_back(value);
        });

    std::sort(matches.begin(), matches.end());
    TALUS_CHECK(count == 2);
    TALUS_CHECK((matches == std::vector<int>{0, 1}));
}

// Test: test_nearest_neighbor_empty_root_returns_null
// Verifies NearestNeighbor reports no value for an empty tree and does not try
// to inspect leaf storage when the root has no entries.
void test_nearest_neighbor_empty_root_returns_null() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root;

    const int* nearest = talus::detail::nearest_neighbor(root, talus::Point<double>{0.0, 0.0});

    TALUS_CHECK(nearest == nullptr);
}

// Test: test_nearest_neighbor_leaf_uses_entry_bounds
// Verifies NearestNeighbor ranks leaf entries by the minimum squared distance
// from the query point to each stored bounding box, not by insertion order.
void test_nearest_neighbor_leaf_uses_entry_bounds() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root;
    root.append_value(Box{{100.0, 100.0}, {101.0, 101.0}}, 100);
    root.append_value(Box{{5.0, 5.0}, {7.0, 7.0}}, 5);
    root.append_value(Box{{20.0, 20.0}, {21.0, 21.0}}, 20);

    const int* nearest = talus::detail::nearest_neighbor(root, talus::Point<double>{6.0, 6.0});

    TALUS_CHECK(nearest != nullptr);
    TALUS_CHECK(*nearest == 5);
}

// Test: test_nearest_neighbor_internal_tree_finds_best_leaf_entry
// Verifies NearestNeighbor descends through internal nodes and finds the closest
// entry in a split-built tree whose root has grown beyond a single leaf.
void test_nearest_neighbor_internal_tree_finds_best_leaf_entry() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 16> pool;
    Node root;

    for (int value : {-100, -50, -10, 0, 25, 50, 100, 150, 200}) {
        auto result = talus::detail::insert_with_split(
            root,
            pool,
            Box{
                {static_cast<double>(value), 0.0},
                {static_cast<double>(value), 0.0}
            },
            value);
        TALUS_CHECK(result.inserted);
    }

    TALUS_CHECK(root.is_internal());

    const int* nearest = talus::detail::nearest_neighbor(root, talus::Point<double>{48.0, 3.0});

    TALUS_CHECK(nearest != nullptr);
    TALUS_CHECK(*nearest == 50);
}

// Test: test_choose_leaf_uses_margin_when_area_is_degenerate
// Verifies ChooseSubtree picks the spatially nearer leaf among collinear
// children whose boxes have zero area. Both candidates are horizontal lines
// (area 0) holding two entries, so overlap enlargement, area enlargement,
// current area, and entry count all tie; only the margin-enlargement tie-breaker
// distinguishes them, selecting the line that grows least to absorb the point.
// Without the margin terms the choice would fall to the first child.
void test_choose_leaf_uses_margin_when_area_is_degenerate() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root(false);
    Node far_line;   // line [0,0]-[5,0]: margin must grow by 5 to reach (10,0)
    Node near_line;  // line [8,0]-[9,0]: margin grows by only 1

    far_line.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 1);
    far_line.append_value(Box{{5.0, 0.0}, {5.0, 0.0}}, 2);
    near_line.append_value(Box{{8.0, 0.0}, {8.0, 0.0}}, 3);
    near_line.append_value(Box{{9.0, 0.0}, {9.0, 0.0}}, 4);

    root.append_child(far_line.bounds(), &far_line);
    root.append_child(near_line.bounds(), &near_line);

    Node* chosen = talus::detail::choose_leaf(root, Box{{10.0, 0.0}, {10.0, 0.0}});

    TALUS_CHECK(chosen == &near_line);
}

// Recursively verifies the structural invariants STR bulk load must establish:
// fill bounds (non-root nodes hold [min_children, max_children] entries),
// correct parent pointers, child entry bounds that match the child's actual
// bounds, node bounds equal to the union of entry bounds, and uniform leaf
// depth. Returns the number of value entries in the subtree.
template<typename Node>
std::size_t verify_packed_subtree(
    const Node& node,
    bool is_root,
    std::size_t depth,
    std::size_t& leaf_depth) {
    constexpr std::size_t no_depth = static_cast<std::size_t>(-1);

    TALUS_CHECK(!node.empty());
    TALUS_CHECK(node.count() <= Node::max_children);
    if (!is_root) {
        TALUS_CHECK(node.count() >= Node::min_children);
    }

    auto combined = node.entry_bounds_at(0);
    for (std::size_t i = 1; i < node.count(); ++i) {
        combined = combined.expand(node.entry_bounds_at(i));
    }
    TALUS_CHECK(node.bounds() == combined);

    if (node.is_leaf()) {
        if (leaf_depth == no_depth) {
            leaf_depth = depth;
        }
        TALUS_CHECK(depth == leaf_depth);
        return node.count();
    }

    std::size_t total = 0;
    for (const auto& entry : node.children()) {
        TALUS_CHECK(entry.child != nullptr);
        TALUS_CHECK(entry.child->parent() == &node);
        TALUS_CHECK(entry.bounds == entry.child->bounds());
        total += verify_packed_subtree(*entry.child, false, depth + 1, leaf_depth);
    }
    return total;
}

template<typename Node>
void verify_packed_tree(const Node& root, std::size_t expected_values) {
    TALUS_CHECK(root.parent() == nullptr);
    std::size_t leaf_depth = static_cast<std::size_t>(-1);
    TALUS_CHECK(verify_packed_subtree(root, true, 0, leaf_depth) == expected_values);
}

// Test: test_bulk_load_group_sizes_borrows_for_min_fill
// Verifies the per-level packing sizes: full groups of capacity, a final
// remainder group, and redistribution from the preceding group when the
// remainder alone would violate the minimum-fill invariant.
void test_bulk_load_group_sizes_borrows_for_min_fill() {
    using talus::detail::bulk_load_detail::group_sizes;

    // Exact multiples produce uniform full groups.
    TALUS_CHECK(group_sizes(8, 4, 2) == (std::vector<std::size_t>{4, 4}));

    // A remainder already at min fill is kept as-is.
    TALUS_CHECK(group_sizes(10, 4, 2) == (std::vector<std::size_t>{4, 4, 2}));

    // A remainder of 1 borrows one entry from the previous group.
    TALUS_CHECK(group_sizes(9, 4, 2) == (std::vector<std::size_t>{4, 3, 2}));

    // A single undersized group is allowed: it becomes the root.
    TALUS_CHECK(group_sizes(1, 4, 2) == (std::vector<std::size_t>{1}));
}

// Test: test_str_bulk_load_single_entry_builds_leaf_root
// Verifies bulk loading one entry produces a leaf root holding exactly that
// entry with matching bounds.
void test_str_bulk_load_single_entry_builds_leaf_root() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node> pool;
    std::vector<Node::value_entry_type> entries;
    entries.emplace_back(Box{{1.0, 2.0}, {3.0, 4.0}}, 7);

    Node* root = talus::detail::str_bulk_load(pool, std::move(entries));

    TALUS_CHECK(root != nullptr);
    TALUS_CHECK(root->is_leaf());
    TALUS_CHECK(root->count() == 1);
    TALUS_CHECK(root->value_at(0).value() == 7);
    TALUS_CHECK(root->bounds() == (Box{{1.0, 2.0}, {3.0, 4.0}}));
    verify_packed_tree(*root, 1);
}

// Test: test_str_bulk_load_full_leaf_stays_single_level
// Verifies that exactly MaxChildren entries pack into one full leaf root with
// no internal level above it.
void test_str_bulk_load_full_leaf_stays_single_level() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node> pool;
    std::vector<Node::value_entry_type> entries;
    for (int i = 0; i < 4; ++i) {
        const double x = static_cast<double>(i);
        entries.emplace_back(Box{{x, 0.0}, {x, 0.0}}, i);
    }

    Node* root = talus::detail::str_bulk_load(pool, std::move(entries));

    TALUS_CHECK(root->is_leaf());
    TALUS_CHECK(root->count() == 4);
    TALUS_CHECK(pool.size() == 1);
    verify_packed_tree(*root, 4);
}

// Test: test_str_bulk_load_overflow_builds_internal_root
// Verifies that one entry past leaf capacity forces a two-level tree whose
// internal root references min-fill-respecting leaves.
void test_str_bulk_load_overflow_builds_internal_root() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node> pool;
    std::vector<Node::value_entry_type> entries;
    for (int i = 0; i < 5; ++i) {
        const double x = static_cast<double>(i);
        entries.emplace_back(Box{{x, 0.0}, {x, 0.0}}, i);
    }

    Node* root = talus::detail::str_bulk_load(pool, std::move(entries));

    TALUS_CHECK(root->is_internal());
    TALUS_CHECK(root->count() == 2);
    verify_packed_tree(*root, 5);
    TALUS_CHECK(talus::detail::count_values(*root) == 5);
}

// Test: test_str_bulk_load_redistributes_underfull_tail_leaf
// Verifies that a remainder smaller than min_children (9 entries with fanout 4
// would leave a 1-entry tail leaf) is fixed by borrowing from the previous
// group, so every non-root node satisfies the minimum-fill invariant.
void test_str_bulk_load_redistributes_underfull_tail_leaf() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node> pool;
    std::vector<Node::value_entry_type> entries;
    for (int i = 0; i < 9; ++i) {
        const double x = static_cast<double>(i);
        entries.emplace_back(Box{{x, 0.0}, {x, 0.0}}, i);
    }

    Node* root = talus::detail::str_bulk_load(pool, std::move(entries));

    verify_packed_tree(*root, 9);
}

// Test: test_str_bulk_load_handles_duplicate_positions
// Verifies bulk loading many identical zero-area boxes still produces a valid
// packed tree (the tie-breaking order is deterministic, so packing cannot
// produce malformed groups).
void test_str_bulk_load_handles_duplicate_positions() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node> pool;
    std::vector<Node::value_entry_type> entries;
    for (int i = 0; i < 50; ++i) {
        entries.emplace_back(Box{{1.0, 1.0}, {1.0, 1.0}}, i);
    }

    Node* root = talus::detail::str_bulk_load(pool, std::move(entries));

    verify_packed_tree(*root, 50);
    TALUS_CHECK(root->bounds() == (Box{{1.0, 1.0}, {1.0, 1.0}}));
}

// Test: test_str_bulk_load_large_set_searchable
// Verifies a multi-level bulk-loaded tree (grid data, several internal levels)
// maintains all structural invariants and that detail::search finds every
// loaded entry exactly once.
void test_str_bulk_load_large_set_searchable() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node> pool;
    std::vector<Node::value_entry_type> entries;
    int id = 0;
    for (int x = 0; x < 25; ++x) {
        for (int y = 0; y < 20; ++y) {
            entries.emplace_back(
                Box{{static_cast<double>(x), static_cast<double>(y)},
                    {static_cast<double>(x), static_cast<double>(y)}},
                id++);
        }
    }

    Node* root = talus::detail::str_bulk_load(pool, std::move(entries));

    verify_packed_tree(*root, 500);

    std::vector<int> found;
    talus::detail::search(*root, Box{{0.0, 0.0}, {25.0, 20.0}}, [&](int value) {
        found.push_back(value);
    });
    std::sort(found.begin(), found.end());
    TALUS_CHECK(found.size() == 500);
    for (int i = 0; i < 500; ++i) {
        TALUS_CHECK(found[static_cast<std::size_t>(i)] == i);
    }

    // Point query for one cell returns exactly that entry.
    std::vector<int> single;
    talus::detail::search(*root, Box{{7.0, 3.0}, {7.0, 3.0}}, [&](int value) {
        single.push_back(value);
    });
    TALUS_CHECK(single == (std::vector<int>{7 * 20 + 3}));
}

// Test: test_str_bulk_load_move_only_values
// Verifies bulk load relocates move-only leaf values without copying.
void test_str_bulk_load_move_only_values() {
    using Node = talus::detail::RTreeNode<MoveOnlyValue, double, 4>;

    talus::detail::PoolAllocator<Node> pool;
    std::vector<Node::value_entry_type> entries;
    for (int i = 0; i < 6; ++i) {
        const double x = static_cast<double>(i);
        entries.emplace_back(Box{{x, 0.0}, {x, 0.0}}, MoveOnlyValue{i});
    }

    Node* root = talus::detail::str_bulk_load(pool, std::move(entries));

    verify_packed_tree(*root, 6);
}

} // namespace

int main() {
    test_overlap_enlargement_no_siblings();
    test_overlap_enlargement_no_new_overlap();
    test_overlap_enlargement_creates_new_overlap();
    test_overlap_enlargement_increases_existing_overlap();
    test_overlap_enlargement_sums_multiple_siblings();
    test_choose_leaf_selects_minimum_enlargement();
    test_choose_leaf_tie_breaks_by_smaller_area();
    test_choose_leaf_tie_breaks_by_fewer_entries();
    test_choose_leaf_prefers_overlap_enlargement_for_leaf_children();
    test_choose_leaf_returns_leaf_root_directly();
    test_choose_leaf_descends_through_internal_nodes();
    test_choose_leaf_uses_area_enlargement_at_internal_level();
    test_choose_leaf_uses_margin_when_area_is_degenerate();
    test_insert_appends_to_root_leaf();
    test_insert_routes_to_child_and_refreshes_ancestor_bounds();
    test_insert_reports_overflow_without_splitting();
    test_insert_refreshes_bounds_three_levels_deep();
    test_split_node_redistributes_leaf_entries();
    test_split_node_redistributes_internal_entries_and_updates_parents();
    test_split_node_resets_internal_sibling_for_leaf_split();
    test_split_node_resets_leaf_sibling_for_internal_split();
    test_split_node_can_choose_y_axis_distribution();
    test_split_node_uses_deterministic_order_for_identical_bounds();
    test_split_node_evaluates_multiple_distributions_for_larger_capacity();
    test_split_node_leaf_bounds_cover_original();
    test_split_node_internal_bounds_cover_original();
    test_split_node_leaf_move_only_values();
    test_adjust_tree_grows_leaf_root_in_place();
    test_adjust_tree_attaches_split_sibling_to_parent();
    test_adjust_tree_propagates_parent_split_to_new_root();
    test_insert_with_split_keeps_tree_valid_after_root_split();
    test_erase_leaf_root_removes_one_matching_entry();
    test_erase_returns_false_when_no_entry_matches();
    test_erase_condenses_underfull_child_and_collapses_root();
    test_erase_move_only_leaf_value();
    test_search_empty_root_returns_no_matches();
    test_search_leaf_reports_intersecting_values();
    test_search_internal_prunes_disjoint_children();
    test_search_split_insert_tree_matches_deterministic_query();
    test_radius_search_empty_root_returns_no_matches();
    test_radius_search_leaf_reports_values_within_radius();
    test_radius_search_internal_prunes_distant_children();
    test_nearest_neighbor_empty_root_returns_null();
    test_nearest_neighbor_leaf_uses_entry_bounds();
    test_nearest_neighbor_internal_tree_finds_best_leaf_entry();
    test_bulk_load_group_sizes_borrows_for_min_fill();
    test_str_bulk_load_single_entry_builds_leaf_root();
    test_str_bulk_load_full_leaf_stays_single_level();
    test_str_bulk_load_overflow_builds_internal_root();
    test_str_bulk_load_redistributes_underfull_tail_leaf();
    test_str_bulk_load_handles_duplicate_positions();
    test_str_bulk_load_large_set_searchable();
    test_str_bulk_load_move_only_values();
}
