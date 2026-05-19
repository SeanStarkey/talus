#include <talus/detail/node.hpp>
#include <talus/detail/pool_alloc.hpp>

#include <cassert>
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

void test_leaf_node_stores_non_default_constructible_values() {
    using Node = talus::detail::RTreeNode<TrackedValue, double, 4>;

    reset_tracked_value_counters();

    {
        Node node;

        static_assert(Node::max_children == 4);
        static_assert(Node::min_children == 2);
        static_assert(Node::entry_capacity == 5);
        static_assert(alignof(Node) == 64);

        assert(node.is_leaf());
        assert(!node.is_internal());
        assert(node.empty());
        assert(node.count() == 0);
        assert(!node.full());
        assert(!node.has_overflow());
        assert(node.underfull());

        TrackedValue alpha{"alpha"};
        node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, alpha);
        node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, TrackedValue{"beta"});

        assert(node.count() == 2);
        assert(node.full() == false);
        assert(node.underfull() == false);
        assert(node.value_at(0).value.name == "alpha");
        assert(node.value_at(1).value.name == "beta");
        assert((node.bounds() == Box{{0.0, 0.0}, {3.0, 3.0}}));
        assert(TrackedValue::copied == 1);
        assert(TrackedValue::moved == 1);

        node.clear();
        assert(node.empty());
        assert(node.bounds() == Box{});
        assert(TrackedValue::destroyed == 3); // temporary beta + two node entries (alpha local is still in scope)
    }

    assert(TrackedValue::constructed == TrackedValue::destroyed);
}

void test_leaf_node_supports_move_only_values_and_overflow_slot() {
    using Node = talus::detail::RTreeNode<MoveOnlyValue, double, 4>;

    Node node;

    node.append_value(Box{{0.0, 0.0}, {0.0, 0.0}}, MoveOnlyValue{0});
    node.append_value(Box{{1.0, 1.0}, {1.0, 1.0}}, MoveOnlyValue{1});
    node.append_value(Box{{2.0, 2.0}, {2.0, 2.0}}, MoveOnlyValue{2});
    assert(!node.full());

    node.append_value(Box{{3.0, 3.0}, {3.0, 3.0}}, MoveOnlyValue{3});
    assert(node.full());
    assert(!node.has_overflow());
    assert(node.can_append_entry());

    node.append_value(Box{{4.0, 4.0}, {4.0, 4.0}}, MoveOnlyValue{4});
    assert(node.count() == Node::entry_capacity);
    assert(node.full());
    assert(node.has_overflow());
    assert(!node.can_append_entry());
    assert(*node.value_at(4).value.value == 4);
    assert((node.bounds() == Box{{0.0, 0.0}, {4.0, 4.0}}));
}

void test_leaf_node_emplaces_immovable_values() {
    using Node = talus::detail::RTreeNode<ImmovableValue, double, 4>;

    Node node;

    auto& entry = node.emplace_value(Box{{-1.0, -1.0}, {1.0, 1.0}}, 42, "immovable");

    assert(node.count() == 1);
    assert(entry.value.id == 42);
    assert(entry.value.label == "immovable");
    assert((node.bounds() == Box{{-1.0, -1.0}, {1.0, 1.0}}));
}

void test_internal_node_tracks_children_and_parent_links() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node left;
    Node right;

    assert(parent.is_internal());
    assert(!parent.is_leaf());

    parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &left);
    parent.append_child(Box{{2.0, 2.0}, {4.0, 4.0}}, &right);

    assert(parent.count() == 2);
    assert(parent.child_at(0).child == &left);
    assert(parent.child_at(1).child == &right);
    assert(left.parent() == &parent);
    assert(right.parent() == &parent);
    assert((parent.bounds() == Box{{0.0, 0.0}, {4.0, 4.0}}));

    parent.update_bounds(0, Box{{-1.0, -2.0}, {1.0, 1.0}});
    assert((parent.child_at(0).bounds == Box{{-1.0, -2.0}, {1.0, 1.0}}));
    assert((parent.bounds() == Box{{-1.0, -2.0}, {4.0, 4.0}}));

    parent.clear();
    assert(parent.empty());
    assert(left.parent() == nullptr);
    assert(right.parent() == nullptr);
}

void test_leaf_node_update_bounds_recomputes_aggregate() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {2.0, 2.0}}, 1);
    node.append_value(Box{{3.0, 3.0}, {5.0, 5.0}}, 2);

    assert((node.bounds() == Box{{0.0, 0.0}, {5.0, 5.0}}));

    node.update_bounds(1, Box{{3.0, 3.0}, {4.0, 4.0}}); // shrink entry 1
    assert((node.value_at(1).bounds == Box{{3.0, 3.0}, {4.0, 4.0}}));
    assert((node.bounds() == Box{{0.0, 0.0}, {4.0, 4.0}}));

    node.update_bounds(0, Box{{-1.0, -1.0}, {2.0, 2.0}}); // expand entry 0
    assert((node.value_at(0).bounds == Box{{-1.0, -1.0}, {2.0, 2.0}}));
    assert((node.bounds() == Box{{-1.0, -1.0}, {4.0, 4.0}}));
}

void test_reset_changes_node_kind_after_destroying_active_entries() {
    using Node = talus::detail::RTreeNode<TrackedValue, double, 4>;

    reset_tracked_value_counters();

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, TrackedValue{"stored"});
    assert(node.is_leaf());

    node.reset_as_internal();
    assert(node.is_internal());
    assert(node.empty());
    assert(TrackedValue::constructed == TrackedValue::destroyed);

    Node child;
    node.append_child(Box{{1.0, 1.0}, {2.0, 2.0}}, &child);
    assert(child.parent() == &node);

    node.reset_as_leaf();
    assert(node.is_leaf());
    assert(node.empty());
    assert(child.parent() == nullptr);
}

void test_remove_at_leaf_swaps_last_into_gap() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 10);
    node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 20);
    node.append_value(Box{{4.0, 4.0}, {5.0, 5.0}}, 30);

    node.remove_at(1); // remove 20; 30 should swap into index 1

    assert(node.count() == 2);
    assert(node.value_at(0).value == 10);
    assert(node.value_at(1).value == 30);
    assert((node.bounds() == Box{{0.0, 0.0}, {5.0, 5.0}}));
}

void test_remove_at_leaf_last_entry() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 10);
    node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 20);

    node.remove_at(1); // no swap needed

    assert(node.count() == 1);
    assert(node.value_at(0).value == 10);
    assert((node.bounds() == Box{{0.0, 0.0}, {1.0, 1.0}}));
}

void test_remove_at_leaf_shrinks_bounds() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 1);
    node.append_value(Box{{5.0, 5.0}, {10.0, 10.0}}, 2); // extends bounds the most
    node.append_value(Box{{0.0, 0.0}, {2.0, 2.0}}, 3);

    assert((node.bounds() == Box{{0.0, 0.0}, {10.0, 10.0}}));

    node.remove_at(1); // remove the large box; entry 3 swaps into index 1

    // remaining: {0,0}-{1,1} and {0,0}-{2,2}
    assert((node.bounds() == Box{{0.0, 0.0}, {2.0, 2.0}}));
}

void test_remove_at_leaf_tracks_lifetimes() {
    using Node = talus::detail::RTreeNode<TrackedValue, double, 4>;

    Node node;
    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, TrackedValue{"first"});
    node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, TrackedValue{"second"});
    node.append_value(Box{{4.0, 4.0}, {5.0, 5.0}}, TrackedValue{"third"});

    reset_tracked_value_counters();

    // remove_at(1): destroys "second", move-constructs "third" into index 1, destroys moved-from "third"
    node.remove_at(1);

    assert(TrackedValue::moved == 1);
    assert(TrackedValue::destroyed == 2); // "second" + moved-from "third"
    assert(TrackedValue::constructed == 1); // the move-construct of "third" into the gap
    assert(node.value_at(0).value.name == "first");
    assert(node.value_at(1).value.name == "third");
}

void test_remove_at_internal_clears_child_parent() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child_a, child_b, child_c;

    parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &child_a);
    parent.append_child(Box{{2.0, 2.0}, {3.0, 3.0}}, &child_b);
    parent.append_child(Box{{4.0, 4.0}, {5.0, 5.0}}, &child_c);

    parent.remove_at(1); // remove child_b; child_c swaps into index 1

    assert(parent.count() == 2);
    assert(child_b.parent() == nullptr);          // removed child's parent cleared
    assert(parent.child_at(0).child == &child_a);
    assert(parent.child_at(1).child == &child_c); // child_c now at index 1
    assert(child_a.parent() == &parent);
    assert(child_c.parent() == &parent);           // still owned by parent
    assert((parent.bounds() == Box{{0.0, 0.0}, {5.0, 5.0}}));
}

void test_reset_clears_own_parent_pointer() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    Node child;

    parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &child);
    assert(child.parent() == &parent);

    child.reset_as_internal();
    assert(child.parent() == nullptr);

    Node new_parent(false);
    new_parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &child);
    assert(child.parent() == &new_parent);

    child.reset_as_leaf();
    assert(child.parent() == nullptr);
}

void test_leaf_values_span_supports_range_iteration() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node node;
    assert(node.values().empty());

    node.append_value(Box{{0.0, 0.0}, {1.0, 1.0}}, 10);
    node.append_value(Box{{2.0, 2.0}, {3.0, 3.0}}, 20);
    node.append_value(Box{{4.0, 4.0}, {5.0, 5.0}}, 30);

    int sum = 0;
    for (auto& entry : node.values()) {
        sum += entry.value;
    }
    assert(sum == 60);
    assert(node.values().size() == 3);

    // const path
    const Node& cnode = node;
    int csum = 0;
    for (const auto& entry : cnode.values()) {
        csum += entry.value;
    }
    assert(csum == 60);
}

void test_internal_children_span_supports_range_iteration() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    Node parent(false);
    assert(parent.children().empty());

    Node a, b, c;
    parent.append_child(Box{{0.0, 0.0}, {1.0, 1.0}}, &a);
    parent.append_child(Box{{2.0, 2.0}, {3.0, 3.0}}, &b);
    parent.append_child(Box{{4.0, 4.0}, {5.0, 5.0}}, &c);

    std::vector<Node*> collected;
    for (auto& entry : parent.children()) {
        collected.push_back(entry.child);
    }
    assert(collected.size() == 3);
    assert(collected[0] == &a);
    assert(collected[1] == &b);
    assert(collected[2] == &c);
}

void test_pool_allocator_returns_aligned_nodes() {
    using Node = talus::detail::RTreeNode<int, double, 4>;

    talus::detail::PoolAllocator<Node, 2> pool;

    Node* first = pool.create();
    Node* second = pool.create(false);

    assert(is_aligned(first, 64));
    assert(is_aligned(second, 64));
    assert(first->is_leaf());
    assert(second->is_internal());
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
}
