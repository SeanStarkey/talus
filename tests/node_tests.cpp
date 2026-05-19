#include <talus/detail/node.hpp>
#include <talus/detail/pool_alloc.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

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
        assert(TrackedValue::destroyed == 3); // alpha variable, temporary beta, and two node entries minus alpha live below.
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
    test_reset_changes_node_kind_after_destroying_active_entries();
    test_pool_allocator_returns_aligned_nodes();
}
