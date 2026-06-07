#include <talus/detail/node.hpp>
#include <talus/detail/pool_alloc.hpp>

#include "test_check.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using Box = talus::BoundingBox<double>;

struct TrackedValue {
    static inline int constructed = 0;
    static inline int destroyed = 0;
    static inline int copied = 0;
    static inline int moved = 0;

    std::string name;

    explicit TrackedValue(std::string value)
        : name(std::move(value)) {
        ++constructed;
    }

    TrackedValue(const TrackedValue& other)
        : name(other.name) {
        ++constructed;
        ++copied;
    }

    TrackedValue(TrackedValue&& other) noexcept
        : name(std::move(other.name)) {
        ++constructed;
        ++moved;
    }

    TrackedValue& operator=(const TrackedValue&) = delete;
    TrackedValue& operator=(TrackedValue&&) = delete;

    ~TrackedValue() {
        ++destroyed;
    }
};

struct MoveOnlyValue {
    std::unique_ptr<int> value;

    explicit MoveOnlyValue(int v)
        : value(std::make_unique<int>(v)) {}

    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

struct ImmovableValue {
    int id = 0;
    std::string label;

    ImmovableValue(int value_id, std::string value_label)
        : id(value_id),
          label(std::move(value_label)) {}

    ImmovableValue(const ImmovableValue&) = delete;
    ImmovableValue& operator=(const ImmovableValue&) = delete;
    ImmovableValue(ImmovableValue&&) = delete;
    ImmovableValue& operator=(ImmovableValue&&) = delete;
};

void reset_tracked_value_counters() {
    TrackedValue::constructed = 0;
    TrackedValue::destroyed = 0;
    TrackedValue::copied = 0;
    TrackedValue::moved = 0;
}

bool is_aligned(const void* ptr, std::size_t alignment) {
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}

// Test: test_leaf_node_stores_non_default_constructible_values
// Verifies leaf nodes construct, store, clear, and destroy non-default values.
void test_leaf_node_stores_non_default_constructible_values() {
    using Node = talus::detail::RTreeNode<TrackedValue, double, 4>;

    reset_tracked_value_counters();

    {
        Node node;

        static_assert(Node::max_children == 4);
        static_assert(Node::min_children == 2);
        static_assert(Node::entry_capacity == 5);
        static_assert(alignof(Node) == 64);

        TALUS_CHECK(node.is_leaf());
        TALUS_CHECK(!node.is_internal());
        TALUS_CHECK(node.empty());
        TALUS_CHECK(node.count() == 0);
        TALUS_CHECK(!node.full());
        TALUS_CHECK(!node.has_overflow());
        TALUS_CHECK(node.underfull());

        TrackedValue alpha{"alpha"};
        node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, alpha);
        node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, TrackedValue{"beta"});

        TALUS_CHECK(node.count() == 2);
        TALUS_CHECK(node.full() == false);
        TALUS_CHECK(node.underfull() == false);
        TALUS_CHECK(node.value_at(0).value().name == "alpha");
        TALUS_CHECK(node.value_at(1).value().name == "beta");
        TALUS_CHECK((node.bounds() == Box{{0.0, 0.0}, {3.0, 3.0}}));
        TALUS_CHECK(TrackedValue::copied == 1);
        TALUS_CHECK(TrackedValue::moved == 1);

        node.clear();
        TALUS_CHECK(node.empty());
        TALUS_CHECK(node.bounds() == Box{});
        TALUS_CHECK(TrackedValue::destroyed == 3); // temporary beta + two node entries (alpha local is still in scope)
    }

    TALUS_CHECK(TrackedValue::constructed == TrackedValue::destroyed);
}

// Test: test_leaf_node_supports_move_only_values_and_overflow_slot
// Verifies leaf nodes support move-only values and the temporary overflow slot.
void test_leaf_node_supports_move_only_values_and_overflow_slot() {
    using Node = talus::detail::RTreeNode<MoveOnlyValue, double, 4>;

    static_assert(Node::can_relocate_value_entries);

    Node node;

    node.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, MoveOnlyValue{0});
    node.append_value(Box{{1.0, 1.0}, {1.0, 1.0}}, MoveOnlyValue{1});
    node.append_value(Box{{2.0, 2.0}, {2.0, 2.0}}, MoveOnlyValue{2});
    TALUS_CHECK(!node.full());

    node.append_value(Box{{3.0, 3.0}, {3.0, 3.0}}, MoveOnlyValue{3});
    TALUS_CHECK(node.full());
    TALUS_CHECK(!node.has_overflow());
    TALUS_CHECK(node.can_append_entry());

    node.append_value(Box{{4.0, 4.0}, {4.0, 4.0}}, MoveOnlyValue{4});
    TALUS_CHECK(node.count() == Node::entry_capacity);
    TALUS_CHECK(node.full());
    TALUS_CHECK(node.has_overflow());
    TALUS_CHECK(!node.can_append_entry());
    TALUS_CHECK(*node.value_at(4).value().value == 4);
    TALUS_CHECK((node.bounds() == Box{{0.0, 0.0}, {4.0, 4.0}}));
}

// Test: test_leaf_node_emplaces_immovable_values
// Verifies immovable values can be emplaced while relocation remains disabled.
void test_leaf_node_emplaces_immovable_values() {
    using Node = talus::detail::RTreeNode<ImmovableValue, double, 4>;

    static_assert(!Node::can_relocate_value_entries);

    Node node;

    auto& entry = node.emplace_value(Box{{-1.0, -1.0}, {1.0, 1.0}}, 42, "immovable");

    TALUS_CHECK(node.count() == 1);
    TALUS_CHECK(entry.value().id == 42);
    TALUS_CHECK(entry.value().label == "immovable");
    TALUS_CHECK((node.bounds() == Box{{-1.0, -1.0}, {1.0, 1.0}}));
}

// Test: test_internal_node_tracks_children_and_parent_links
// Verifies internal nodes store child entries and maintain child parent pointers.
void test_internal_node_tracks_children_and_parent_links() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node left;
    Node right;

    TALUS_CHECK(parent.is_internal());
    TALUS_CHECK(!parent.is_leaf());

    parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &left);
    parent.append_child(Box{{2.0, 2.0}, {4.0, 4.0}}, &right);

    TALUS_CHECK(parent.count() == 2);
    TALUS_CHECK(parent.child_at(0).child == &left);
    TALUS_CHECK(parent.child_at(1).child == &right);
    TALUS_CHECK(left.parent() == &parent);
    TALUS_CHECK(right.parent() == &parent);
    TALUS_CHECK((parent.bounds() == Box{{0.0, 0.0}, {4.0, 4.0}}));

    parent.update_bounds(0, Box{{-1.0, -2.0}, {1.0, 1.0}});
    TALUS_CHECK((parent.child_at(0).bounds == Box{{-1.0, -2.0}, {1.0, 1.0}}));
    TALUS_CHECK((parent.bounds() == Box{{-1.0, -2.0}, {4.0, 4.0}}));

    parent.clear();
    TALUS_CHECK(parent.empty());
    TALUS_CHECK(left.parent() == nullptr);
    TALUS_CHECK(right.parent() == nullptr);
}

// Test: test_leaf_node_update_bounds_recomputes_aggregate
// Verifies updating entry bounds recomputes the node aggregate bounds.
void test_leaf_node_update_bounds_recomputes_aggregate() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {2.0, 2.0}}, 1);
    node.append_value(Box{{3.0, 3.0}, {5.0, 5.0}}, 2);

    TALUS_CHECK((node.bounds() == Box{{0.0, 0.0}, {5.0, 5.0}}));

    node.update_bounds(1, Box{{3.0, 3.0}, {4.0, 4.0}}); // shrink entry 1
    TALUS_CHECK((node.value_at(1).bounds == Box{{3.0, 3.0}, {4.0, 4.0}}));
    TALUS_CHECK((node.bounds() == Box{{0.0, 0.0}, {4.0, 4.0}}));

    node.update_bounds(0, Box{{-1.0, -1.0}, {2.0, 2.0}}); // expand entry 0
    TALUS_CHECK((node.value_at(0).bounds == Box{{-1.0, -1.0}, {2.0, 2.0}}));
    TALUS_CHECK((node.bounds() == Box{{-1.0, -1.0}, {4.0, 4.0}}));
}

// Test: test_reset_changes_node_kind_after_destroying_active_entries
// Verifies reset switches node kind after destroying entries from the prior mode.
void test_reset_changes_node_kind_after_destroying_active_entries() {
    using Node = talus::detail::RTreeNode<TrackedValue, double, 4>;

    reset_tracked_value_counters();

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, TrackedValue{"stored"});
    TALUS_CHECK(node.is_leaf());

    node.reset_as_internal();
    TALUS_CHECK(node.is_internal());
    TALUS_CHECK(node.empty());
    TALUS_CHECK(TrackedValue::constructed == TrackedValue::destroyed);

    Node child;
    node.append_child(Box{{1.0, 1.0}, {2.0, 2.0}}, &child);
    TALUS_CHECK(child.parent() == &node);

    node.reset_as_leaf();
    TALUS_CHECK(node.is_leaf());
    TALUS_CHECK(node.empty());
    TALUS_CHECK(child.parent() == nullptr);
}

// Test: test_remove_at_leaf_swaps_last_into_gap
// Verifies leaf removal compacts by moving the last entry into the removed slot.
void test_remove_at_leaf_swaps_last_into_gap() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 10);
    node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 20);
    node.append_value(Box{{4.0, 4.0}, {5.0, 5.0}}, 30);

    node.remove_at(1); // remove 20; 30 should swap into index 1

    TALUS_CHECK(node.count() == 2);
    TALUS_CHECK(node.value_at(0).value() == 10);
    TALUS_CHECK(node.value_at(1).value() == 30);
    TALUS_CHECK((node.bounds() == Box{{0.0, 0.0}, {5.0, 5.0}}));
}

// Test: test_remove_at_leaf_last_entry
// Verifies removing the final leaf entry preserves remaining entries and bounds.
void test_remove_at_leaf_last_entry() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 10);
    node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 20);

    node.remove_at(1); // no swap needed

    TALUS_CHECK(node.count() == 1);
    TALUS_CHECK(node.value_at(0).value() == 10);
    TALUS_CHECK((node.bounds() == Box{{0.0, 0.0}, {1.0, 1.0}}));
}

// Test: test_remove_at_leaf_shrinks_bounds
// Verifies leaf removal shrinks aggregate bounds when the removed entry was extreme.
void test_remove_at_leaf_shrinks_bounds() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    node.append_value(Box{{5.0, 5.0}, {10.0, 10.0}}, 2); // extends bounds the most
    node.append_value(Box{{0.0, 0.0}, {2.0, 2.0}}, 3);

    TALUS_CHECK((node.bounds() == Box{{0.0, 0.0}, {10.0, 10.0}}));

    node.remove_at(1); // remove the large box; entry 3 swaps into index 1

    // remaining: {0,0}-{1,1} and {0,0}-{2,2}
    TALUS_CHECK((node.bounds() == Box{{0.0, 0.0}, {2.0, 2.0}}));
}

// Test: test_remove_at_leaf_tracks_lifetimes
// Verifies leaf removal destroys removed values and preserves remaining lifetimes.
void test_remove_at_leaf_tracks_lifetimes() {
    using Node = talus::detail::RTreeNode<TrackedValue, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, TrackedValue{"first"});
    node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, TrackedValue{"second"});
    node.append_value(Box{{4.0, 4.0}, {5.0, 5.0}}, TrackedValue{"third"});

    reset_tracked_value_counters();

    // remove_at(1): destroys "second", move-constructs "third" into index 1, destroys moved-from "third"
    node.remove_at(1);

    TALUS_CHECK(TrackedValue::moved == 1);
    TALUS_CHECK(TrackedValue::destroyed == 2); // "second" + moved-from "third"
    TALUS_CHECK(TrackedValue::constructed == 1); // the move-construct of "third" into the gap
    TALUS_CHECK(node.value_at(0).value().name == "first");
    TALUS_CHECK(node.value_at(1).value().name == "third");
}

// Test: test_remove_at_internal_clears_child_parent
// Verifies internal removal detaches the removed child and preserves moved-child parent links.
void test_remove_at_internal_clears_child_parent() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child_a, child_b, child_c;

    parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &child_a);
    parent.append_child(Box{{2.0, 2.0}, {3.0, 3.0}}, &child_b);
    parent.append_child(Box{{4.0, 4.0}, {5.0, 5.0}}, &child_c);

    parent.remove_at(1); // remove child_b; child_c swaps into index 1

    TALUS_CHECK(parent.count() == 2);
    TALUS_CHECK(child_b.parent() == nullptr);          // removed child's parent cleared
    TALUS_CHECK(parent.child_at(0).child == &child_a);
    TALUS_CHECK(parent.child_at(1).child == &child_c); // child_c now at index 1
    TALUS_CHECK(child_a.parent() == &parent);
    TALUS_CHECK(child_c.parent() == &parent);           // still owned by parent
    TALUS_CHECK((parent.bounds() == Box{{0.0, 0.0}, {5.0, 5.0}}));
}

// Test: test_reset_clears_own_parent_pointer
// Verifies resetting a node clears its own parent pointer.
void test_reset_clears_own_parent_pointer() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child;

    parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &child);
    TALUS_CHECK(child.parent() == &parent);

    child.reset_as_internal();
    TALUS_CHECK(child.parent() == nullptr);

    Node new_parent(false);
    new_parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &child);
    TALUS_CHECK(child.parent() == &new_parent);

    child.reset_as_leaf();
    TALUS_CHECK(child.parent() == nullptr);
}

// Test: test_leaf_values_span_supports_range_iteration
// Verifies leaf value spans expose live entries for range iteration and mutation.
void test_leaf_values_span_supports_range_iteration() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    TALUS_CHECK(node.values().empty());

    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 10);
    node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 20);
    node.append_value(Box{{4.0, 4.0}, {5.0, 5.0}}, 30);

    int sum = 0;
    for (auto& entry : node.values()) {
        sum += entry.value();
    }
    TALUS_CHECK(sum == 60);
    TALUS_CHECK(node.values().size() == 3);

    // const path
    const Node& cnode = node;
    int csum = 0;
    for (const auto& entry : cnode.values()) {
        csum += entry.value();
    }
    TALUS_CHECK(csum == 60);
}

// Test: test_internal_children_span_supports_range_iteration
// Verifies internal child spans expose live child entries for range iteration.
void test_internal_children_span_supports_range_iteration() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    TALUS_CHECK(parent.children().empty());

    Node a, b, c;
    parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &a);
    parent.append_child(Box{{2.0, 2.0}, {3.0, 3.0}}, &b);
    parent.append_child(Box{{4.0, 4.0}, {5.0, 5.0}}, &c);

    std::vector<Node*> collected;
    for (auto& entry : parent.children()) {
        collected.push_back(entry.child);
    }
    TALUS_CHECK(collected.size() == 3);
    TALUS_CHECK(collected[0] == &a);
    TALUS_CHECK(collected[1] == &b);
    TALUS_CHECK(collected[2] == &c);
}

// Test: test_pool_allocator_returns_aligned_nodes
// Verifies pool allocation preserves RTreeNode cache-line alignment.
void test_pool_allocator_returns_aligned_nodes() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 2> pool;

    Node* first = pool.create();
    Node* second = pool.create(false);

    TALUS_CHECK(is_aligned(first, 64));
    TALUS_CHECK(is_aligned(second, 64));
    TALUS_CHECK(first->is_leaf());
    TALUS_CHECK(second->is_internal());
}

// Test: test_leaf_node_boxes_large_values
// Verifies large value types are stored out of line (boxed behind a pointer) so
// the node stays small — the key property of the adaptive storage. A small type
// stays inline. The boxed path must still construct, retrieve, and relocate
// values correctly (overflow slot, then a move via remove_at).
void test_leaf_node_boxes_large_values() {
    struct Big {
        int id = 0;
        std::array<char, 256> blob{};
    };

    // The storage decision: Big is boxed, int stays inline.
    static_assert(talus::detail::rtree_boxes_value<Big>);
    static_assert(!talus::detail::rtree_boxes_value<int>);

    using BigNode = talus::detail::RTreeNode<Big, double, 9>;
    // The payoff: a 260-byte value type does NOT make the node ~10x larger.
    // Boxed leaf entries are pointer-sized (like child entries), so the node
    // stays modest; inline storage would push this well past 2.5 KB.
    static_assert(sizeof(BigNode) < 1024,
        "large values must be boxed so the node does not carry inline payload");

    BigNode node;  // leaf
    for (int i = 0; i < static_cast<int>(BigNode::entry_capacity); ++i) {
        Big b;
        b.id = i;
        b.blob[0] = static_cast<char>(i);
        const auto c = static_cast<double>(i);
        node.append_value(Box{{c, 0.0}, {c, 0.0}}, std::move(b));
    }
    TALUS_CHECK(node.count() == BigNode::entry_capacity);
    TALUS_CHECK(node.value_at(3).value().id == 3);
    TALUS_CHECK(node.value_at(3).value().blob[0] == static_cast<char>(3));

    // Relocation moves the box (pointer), not the 256-byte payload.
    node.remove_at(0);
    TALUS_CHECK(node.count() == BigNode::entry_capacity - 1);
    for (const auto& entry : node.values()) {
        TALUS_CHECK(entry.value().blob[0] == static_cast<char>(entry.value().id));
    }
}

} // namespace

int main() {
    test_leaf_node_stores_non_default_constructible_values();
    test_leaf_node_supports_move_only_values_and_overflow_slot();
    test_leaf_node_emplaces_immovable_values();
    test_internal_node_tracks_children_and_parent_links();
    test_leaf_node_update_bounds_recomputes_aggregate();
    test_reset_changes_node_kind_after_destroying_active_entries();
    test_remove_at_leaf_swaps_last_into_gap();
    test_remove_at_leaf_last_entry();
    test_remove_at_leaf_shrinks_bounds();
    test_remove_at_leaf_tracks_lifetimes();
    test_remove_at_internal_clears_child_parent();
    test_reset_clears_own_parent_pointer();
    test_leaf_values_span_supports_range_iteration();
    test_internal_children_span_supports_range_iteration();
    test_pool_allocator_returns_aligned_nodes();
    test_leaf_node_boxes_large_values();
}
