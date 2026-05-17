#include <talus/detail/pool_alloc.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

struct alignas(64) NodeLike {
    static inline int constructed = 0;
    static inline int destroyed = 0;

    int value = 0;

    explicit NodeLike(int v) noexcept
        : value(v) {
        ++constructed;
    }

    ~NodeLike() {
        ++destroyed;
    }
};

struct ThrowingNode {
    static inline bool throw_on_construct = false;
    static inline int destroyed = 0;

    int value = 0;

    explicit ThrowingNode(int v)
        : value(v) {
        if (throw_on_construct) {
            throw_on_construct = false;
            throw std::runtime_error{"construction failed"};
        }
    }

    ~ThrowingNode() {
        ++destroyed;
    }
};

void reset_counters() {
    NodeLike::constructed = 0;
    NodeLike::destroyed = 0;
}

bool is_aligned(const void* ptr, std::size_t alignment) {
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}

void test_create_alignment_and_growth() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    NodeLike* first = pool.create(1);
    NodeLike* second = pool.create(2);
    NodeLike* third = pool.create(3);

    assert(first->value == 1);
    assert(second->value == 2);
    assert(third->value == 3);
    assert(reinterpret_cast<std::byte*>(second) - reinterpret_cast<std::byte*>(first)
           == static_cast<std::ptrdiff_t>(sizeof(NodeLike)));
    assert(is_aligned(first, alignof(NodeLike)));
    assert(is_aligned(second, alignof(NodeLike)));
    assert(is_aligned(third, alignof(NodeLike)));
    assert(pool.size() == 3);
    assert(!pool.empty());
    assert(pool.block_count() == 2);
    assert(pool.capacity() == 4);
    assert(NodeLike::constructed == 3);
    assert(NodeLike::destroyed == 0);
}

void test_destroy_reuses_slots() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    NodeLike* first = pool.create(1);
    NodeLike* second = pool.create(2);

    pool.destroy(first);
    assert(pool.size() == 1);
    assert(NodeLike::destroyed == 1);

    NodeLike* reused = pool.create(3);
    assert(reused == first);
    assert(reused->value == 3);
    assert(pool.size() == 2);
    assert(second->value == 2);
    assert(NodeLike::constructed == 3);

    pool.clear();
    assert(pool.empty());
    assert(pool.block_count() == 0);
    assert(pool.capacity() == 0);
    assert(NodeLike::destroyed == 3);
}

void test_reset_keeps_capacity() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    NodeLike* first = pool.create(1);
    NodeLike* second = pool.create(2);
    NodeLike* third = pool.create(3);
    (void)second;
    (void)third;

    assert(pool.block_count() == 2);
    assert(pool.capacity() == 4);

    pool.reset();
    assert(pool.empty());
    assert(pool.block_count() == 2);
    assert(pool.capacity() == 4);
    assert(NodeLike::destroyed == 3);

    NodeLike* after_reset = pool.create(4);
    assert(after_reset == first);
    assert(after_reset->value == 4);
    assert(pool.size() == 1);
    assert(pool.block_count() == 2);
    assert(NodeLike::constructed == 4);
}

void test_null_destroy_is_noop() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike> pool;

    pool.destroy(nullptr);
    assert(pool.empty());
    assert(NodeLike::constructed == 0);
    assert(NodeLike::destroyed == 0);
}

void test_throwing_constructor_releases_slot() {
    ThrowingNode::throw_on_construct = false;
    ThrowingNode::destroyed = 0;

    talus::detail::PoolAllocator<ThrowingNode, 2> pool;

    ThrowingNode::throw_on_construct = true;
    try {
        (void)pool.create(1);
        assert(false);
    } catch (const std::runtime_error&) {
    }

    assert(pool.empty());
    assert(pool.capacity() == 2);

    ThrowingNode* first = pool.create(2);
    assert(first->value == 2);
    assert(pool.size() == 1);

    pool.destroy(first);
    assert(pool.empty());
    assert(ThrowingNode::destroyed == 1);

    ThrowingNode::throw_on_construct = true;
    try {
        (void)pool.create(3);
        assert(false);
    } catch (const std::runtime_error&) {
    }

    assert(pool.empty());
    assert(ThrowingNode::destroyed == 1);

    ThrowingNode* reused = pool.create(4);
    assert(reused == first);
    assert(reused->value == 4);
    assert(pool.size() == 1);
}

} // namespace

int main() {
    test_create_alignment_and_growth();
    test_destroy_reuses_slots();
    test_reset_keeps_capacity();
    test_null_destroy_is_noop();
    test_throwing_constructor_releases_slot();
}
