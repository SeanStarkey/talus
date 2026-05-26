#include <talus/detail/algorithms.hpp>
#include <talus/detail/node.hpp>

#include <cassert>
#include <string>

namespace {

using Box = talus::BoundingBox<double>;

struct Payload {
    int id = 0;
    std::string label;
};

void test_overlap_enlargement_no_siblings() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child;
    child.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    parent.append_child(child.bounds(), &child);

    double delta = talus::detail::overlap_enlargement(parent, 0, Box{{0.0, 0.0}, {5.0, 5.0}});
    assert(delta == 0.0);
}

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
    assert(delta == 0.0);
}

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
    assert(delta == 0.5);
}

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
    assert(delta == 2.0);
}

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
    assert(delta == 1.0);
}

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

    assert(chosen == &right);
}

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

    assert(chosen == &small);
}

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

    assert(chosen == &sparse);
}

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

    assert(chosen == &overlap_preferred_by_rstar);
}

void test_insert_appends_to_root_leaf() {
    using Node = talus::detail::RTreeNode<Payload, double, 4>;

    Node root;

    auto result = talus::detail::insert(root, Box{{1.0, 2.0}, {1.0, 2.0}}, Payload{7, "root"});

    assert(result.inserted);
    assert(!result.needs_split());
    assert(result.leaf == &root);
    assert(root.count() == 1);
    assert(root.value_at(0).value.id == 7);
    assert(root.value_at(0).value.label == "root");
    assert((root.bounds() == Box{{1.0, 2.0}, {1.0, 2.0}}));
}

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

    assert(result.inserted);
    assert(result.leaf == &right);
    assert(right.count() == 2);
    assert(right.value_at(1).value == 3);
    assert((right.bounds() == Box{{10.0, 10.0}, {13.0, 13.0}}));
    assert((root.child_at(1).bounds == right.bounds()));
    assert((root.bounds() == Box{{0.0, 0.0}, {13.0, 13.0}}));
}

void test_insert_reports_overflow_without_splitting() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root;
    root.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, 0);
    root.append_value(Box{{1.0, 1.0}, {1.0, 1.0}}, 1);
    root.append_value(Box{{2.0, 2.0}, {2.0, 2.0}}, 2);
    root.append_value(Box{{3.0, 3.0}, {3.0, 3.0}}, 3);

    auto result = talus::detail::insert(root, Box{{4.0, 4.0}, {4.0, 4.0}}, 4);

    assert(result.inserted);
    assert(result.needs_split());
    assert(result.overflow == &root);
    assert(root.has_overflow());
    assert(root.count() == Node::entry_capacity);
}

// Verifies that refresh_ancestor_bounds propagates through more than one level.
// Tree shape: root(internal) -> mid(internal) -> leaf
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

    assert(result.inserted);
    assert(result.leaf == &leaf);

    const Box expected{{0.0, 0.0}, {6.0, 6.0}};
    assert((leaf.bounds() == expected));
    assert((mid.child_at(0).bounds == expected));   // mid's stored child bounds updated
    assert((root.child_at(0).bounds == expected));  // root's stored child bounds updated
}

// choose_leaf on a leaf root returns it immediately without descending.
void test_choose_leaf_returns_leaf_root_directly() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node root; // leaf by default
    root.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);

    Node* chosen = talus::detail::choose_leaf(root, Box{{5.0, 5.0}, {5.0, 5.0}});

    assert(chosen == &root);
}

// Three-level tree: root(internal) → branch(internal) → leaf.
// First level uses area-enlargement (children of root are internal, not leaves).
// Second level uses overlap-enlargement (children of branch are leaves).
// Verifies that choose_leaf traverses both levels and lands on the right leaf.
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

    assert(chosen == &leaf_far);
}

// Three-level tree where the first-level decision is forced by area-enlargement,
// not overlap-enlargement. branch_a has a large existing area so inserting a point
// inside it costs zero area growth; branch_b would require growth. At the root level
// the children are internal nodes (area-enlargement mode), so branch_a is chosen
// even though it has higher sibling overlap, which is what an overlap-first strategy
// would penalise.
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

    assert(chosen == &leaf_a);
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
    test_insert_appends_to_root_leaf();
    test_insert_routes_to_child_and_refreshes_ancestor_bounds();
    test_insert_reports_overflow_without_splitting();
    test_insert_refreshes_bounds_three_levels_deep();
}
