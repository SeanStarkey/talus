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

} // namespace

int main() {
    test_choose_leaf_selects_minimum_enlargement();
    test_choose_leaf_tie_breaks_by_smaller_area();
    test_choose_leaf_tie_breaks_by_fewer_entries();
    test_choose_leaf_prefers_overlap_enlargement_for_leaf_children();
    test_insert_appends_to_root_leaf();
    test_insert_routes_to_child_and_refreshes_ancestor_bounds();
    test_insert_reports_overflow_without_splitting();
    test_insert_refreshes_bounds_three_levels_deep();
}
