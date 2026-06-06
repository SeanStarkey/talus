#include <talus/detail/pool_alloc.hpp>

#include "test_check.hpp"
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

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

// Test: test_create_alignment_and_growth
// Verifies created objects are correctly aligned and capacity grows by blocks.
void test_create_alignment_and_growth() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    NodeLike* first = pool.create(1);
    NodeLike* second = pool.create(2);
    NodeLike* third = pool.create(3);

    TALUS_CHECK(first->value == 1);
    TALUS_CHECK(second->value == 2);
    TALUS_CHECK(third->value == 3);
    TALUS_CHECK(reinterpret_cast<std::byte*>(second) - reinterpret_cast<std::byte*>(first)
           == static_cast<std::ptrdiff_t>(sizeof(NodeLike)));
    TALUS_CHECK(is_aligned(first, alignof(NodeLike)));
    TALUS_CHECK(is_aligned(second, alignof(NodeLike)));
    TALUS_CHECK(is_aligned(third, alignof(NodeLike)));
    TALUS_CHECK(pool.size() == 3);
    TALUS_CHECK(!pool.empty());
    TALUS_CHECK(pool.block_count() == 2);
    TALUS_CHECK(pool.capacity() == 4);
    TALUS_CHECK(NodeLike::constructed == 3);
    TALUS_CHECK(NodeLike::destroyed == 0);
}

// Test: test_destroy_reuses_slots
// Verifies destroyed slots are returned to the free list and reused.
void test_destroy_reuses_slots() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    NodeLike* first = pool.create(1);
    NodeLike* second = pool.create(2);

    pool.destroy(first);
    TALUS_CHECK(pool.size() == 1);
    TALUS_CHECK(NodeLike::destroyed == 1);

    NodeLike* reused = pool.create(3);
    TALUS_CHECK(reused == first);
    TALUS_CHECK(reused->value == 3);
    TALUS_CHECK(pool.size() == 2);
    TALUS_CHECK(second->value == 2);
    TALUS_CHECK(NodeLike::constructed == 3);

    pool.clear();
    TALUS_CHECK(pool.empty());
    TALUS_CHECK(pool.block_count() == 0);
    TALUS_CHECK(pool.capacity() == 0);
    TALUS_CHECK(NodeLike::destroyed == 3);
}

// Test: test_reset_keeps_capacity
// Verifies reset destroys live objects while retaining allocated block capacity.
void test_reset_keeps_capacity() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    NodeLike* first = pool.create(1);
    NodeLike* second = pool.create(2);
    NodeLike* third = pool.create(3);
    (void)second;
    (void)third;

    TALUS_CHECK(pool.block_count() == 2);
    TALUS_CHECK(pool.capacity() == 4);

    pool.reset();
    TALUS_CHECK(pool.empty());
    TALUS_CHECK(pool.block_count() == 2);
    TALUS_CHECK(pool.capacity() == 4);
    TALUS_CHECK(NodeLike::destroyed == 3);

    NodeLike* after_reset = pool.create(4);
    TALUS_CHECK(after_reset == first);
    TALUS_CHECK(after_reset->value == 4);
    TALUS_CHECK(pool.size() == 1);
    TALUS_CHECK(pool.block_count() == 2);
    TALUS_CHECK(NodeLike::constructed == 4);
}

// Test: test_null_destroy_is_noop
// Verifies destroying a null pointer leaves the pool unchanged.
void test_null_destroy_is_noop() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike> pool;

    pool.destroy(nullptr);
    TALUS_CHECK(pool.empty());
    TALUS_CHECK(NodeLike::constructed == 0);
    TALUS_CHECK(NodeLike::destroyed == 0);
}

void run_destroy_interior_pointer_case() {
    talus::detail::PoolAllocator<NodeLike, 2> pool;
    NodeLike* object = pool.create(1);
    auto* interior = reinterpret_cast<NodeLike*>(reinterpret_cast<std::byte*>(object) + 1);

    pool.destroy(interior);
}

// Test: test_destroy_rejects_interior_pointer
// Verifies destroy rejects interior pointers instead of treating them as owned slots.
void test_destroy_rejects_interior_pointer(const char* executable) {
    const std::string command = std::string{"\""} + executable + "\" --destroy-interior-pointer";
    int status = std::system(command.c_str());
    TALUS_CHECK(status != 0);
}

// Test: test_reserve_preallocates_without_construction
// Verifies reserve allocates raw capacity without constructing objects.
void test_reserve_preallocates_without_construction() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    pool.reserve(0);
    TALUS_CHECK(pool.empty());
    TALUS_CHECK(pool.block_count() == 0);
    TALUS_CHECK(pool.capacity() == 0);

    pool.reserve(5);
    TALUS_CHECK(pool.empty());
    TALUS_CHECK(pool.block_count() == 3);
    TALUS_CHECK(pool.capacity() == 6);
    TALUS_CHECK(NodeLike::constructed == 0);
    TALUS_CHECK(NodeLike::destroyed == 0);

    pool.reserve(3);
    TALUS_CHECK(pool.block_count() == 3);
    TALUS_CHECK(pool.capacity() == 6);
}

// Test: test_reserve_block_count_does_not_wrap
// Verifies reserve handles maximum object counts without block-count overflow.
void test_reserve_block_count_does_not_wrap() {
    talus::detail::PoolAllocator<NodeLike, 2> pool;

    try {
        pool.reserve(std::numeric_limits<std::size_t>::max());
        TALUS_CHECK(false);
    } catch (const std::length_error&) {
    }

    TALUS_CHECK(pool.empty());
    TALUS_CHECK(pool.block_count() == 0);
    TALUS_CHECK(pool.capacity() == 0);
}

// Test: test_create_uses_reserved_blocks_before_growing
// Verifies create consumes reserved capacity before allocating another block.
void test_create_uses_reserved_blocks_before_growing() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;
    pool.reserve(4);

    NodeLike* first = pool.create(1);
    NodeLike* second = pool.create(2);
    NodeLike* third = pool.create(3);
    NodeLike* fourth = pool.create(4);

    TALUS_CHECK(first->value == 1);
    TALUS_CHECK(second->value == 2);
    TALUS_CHECK(third->value == 3);
    TALUS_CHECK(fourth->value == 4);
    TALUS_CHECK(pool.size() == 4);
    TALUS_CHECK(pool.block_count() == 2);
    TALUS_CHECK(pool.capacity() == 4);

    (void)pool.create(5);
    TALUS_CHECK(pool.size() == 5);
    TALUS_CHECK(pool.block_count() == 3);
    TALUS_CHECK(pool.capacity() == 6);

    pool.clear();
    TALUS_CHECK(NodeLike::constructed == 5);
    TALUS_CHECK(NodeLike::destroyed == 5);
}

// Test: test_reserve_preserves_live_objects
// Verifies reserve keeps existing live objects valid while adding capacity.
void test_reserve_preserves_live_objects() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    NodeLike* first = pool.create(10);
    NodeLike* second = pool.create(20);

    pool.reserve(5);
    TALUS_CHECK(pool.size() == 2);
    TALUS_CHECK(pool.block_count() == 3);
    TALUS_CHECK(pool.capacity() == 6);
    TALUS_CHECK(first->value == 10);
    TALUS_CHECK(second->value == 20);
    TALUS_CHECK(NodeLike::constructed == 2);
    TALUS_CHECK(NodeLike::destroyed == 0);

    NodeLike* third = pool.create(30);
    TALUS_CHECK(third != first);
    TALUS_CHECK(third != second);
    TALUS_CHECK(third->value == 30);
    TALUS_CHECK(pool.size() == 3);
    TALUS_CHECK(pool.block_count() == 3);

    pool.clear();
    TALUS_CHECK(NodeLike::destroyed == 3);
}

// Test: test_move_semantics
// Verifies moving a pool transfers ownership, live objects, and reusable slots.
void test_move_semantics() {
    reset_counters();

    // Move construction transfers ownership of all blocks and live objects.
    {
        talus::detail::PoolAllocator<NodeLike, 2> src;
        NodeLike* a = src.create(10);
        (void)src.create(20);

        talus::detail::PoolAllocator<NodeLike, 2> dst(std::move(src));

        TALUS_CHECK(dst.size() == 2);
        TALUS_CHECK(dst.block_count() == 1);
        TALUS_CHECK(a->value == 10);
        TALUS_CHECK(NodeLike::constructed == 2);
        TALUS_CHECK(NodeLike::destroyed == 0);
        // src is valid but unspecified (defaulted move ctor); only dst owns and destructs the blocks.
    }
    TALUS_CHECK(NodeLike::destroyed == 2);

    reset_counters();

    // Move assignment destroys the destination's existing objects, then takes ownership.
    {
        talus::detail::PoolAllocator<NodeLike, 2> src;
        (void)src.create(1);
        (void)src.create(2);

        talus::detail::PoolAllocator<NodeLike, 2> dst;
        (void)dst.create(99);

        dst = std::move(src);

        TALUS_CHECK(NodeLike::destroyed == 1);  // dst's pre-existing object destroyed by clear()
        TALUS_CHECK(dst.size() == 2);
        TALUS_CHECK(dst.block_count() == 1);
        // The move assignment operator explicitly resets the moved-from state.
        TALUS_CHECK(src.size() == 0);
        TALUS_CHECK(src.empty());
        TALUS_CHECK(src.block_count() == 0);
    }
    TALUS_CHECK(NodeLike::destroyed == 3);
}

// Test: test_interleaved_free_list_and_sequential
// Verifies free-list reuse works correctly alongside sequential allocation.
void test_interleaved_free_list_and_sequential() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 4> pool;

    NodeLike* a = pool.create(1);  // sequential slot 0
    NodeLike* b = pool.create(2);  // sequential slot 1
    NodeLike* c = pool.create(3);  // sequential slot 2

    pool.destroy(b);
    TALUS_CHECK(pool.size() == 2);
    TALUS_CHECK(NodeLike::destroyed == 1);

    // Free list (LIFO) yields b's slot before advancing the sequential cursor.
    NodeLike* d = pool.create(4);
    TALUS_CHECK(d == b);
    TALUS_CHECK(d->value == 4);
    TALUS_CHECK(pool.size() == 3);

    // Sequential cursor resumes at slot 3, skipping the reused slot.
    NodeLike* e = pool.create(5);
    TALUS_CHECK(e != a && e != c && e != d);
    TALUS_CHECK(e->value == 5);
    TALUS_CHECK(pool.size() == 4);

    pool.destroy(c);  // free_list = [c]
    pool.destroy(a);  // free_list = [c, a]

    NodeLike* f = pool.create(6);
    TALUS_CHECK(f == a);  // LIFO: a was pushed last, returned first
    NodeLike* g = pool.create(7);
    TALUS_CHECK(g == c);  // LIFO: c was pushed first, returned last

    TALUS_CHECK(pool.size() == 4);

    pool.clear();
    TALUS_CHECK(NodeLike::constructed == 7);
    TALUS_CHECK(NodeLike::destroyed == 7);
}

// Test: test_reset_refills_all_blocks
// Verifies reset makes every previously allocated block available for reuse.
void test_reset_refills_all_blocks() {
    reset_counters();

    talus::detail::PoolAllocator<NodeLike, 2> pool;

    NodeLike* a = pool.create(1);  // block 0, slot 0
    NodeLike* b = pool.create(2);  // block 0, slot 1
    NodeLike* c = pool.create(3);  // block 1, slot 0
    NodeLike* d = pool.create(4);  // block 1, slot 1

    TALUS_CHECK(pool.block_count() == 2);
    TALUS_CHECK(pool.capacity() == 4);
    TALUS_CHECK(pool.size() == 4);

    pool.reset();
    TALUS_CHECK(pool.empty());
    TALUS_CHECK(pool.block_count() == 2);
    TALUS_CHECK(pool.capacity() == 4);
    TALUS_CHECK(NodeLike::destroyed == 4);

    // Sequential allocation after reset replays through all existing blocks without growing.
    NodeLike* a2 = pool.create(10);
    NodeLike* b2 = pool.create(20);
    NodeLike* c2 = pool.create(30);
    NodeLike* d2 = pool.create(40);

    TALUS_CHECK(a2 == a);
    TALUS_CHECK(b2 == b);
    TALUS_CHECK(c2 == c);
    TALUS_CHECK(d2 == d);
    TALUS_CHECK(pool.size() == 4);
    TALUS_CHECK(pool.block_count() == 2);
    TALUS_CHECK(NodeLike::constructed == 8);

    pool.clear();
    TALUS_CHECK(NodeLike::destroyed == 8);
}

// Test: test_throwing_constructor_releases_slot
// Verifies failed construction releases the acquired slot and preserves pool state.
void test_throwing_constructor_releases_slot() {
    ThrowingNode::throw_on_construct = false;
    ThrowingNode::destroyed = 0;

    talus::detail::PoolAllocator<ThrowingNode, 2> pool;

    ThrowingNode::throw_on_construct = true;
    try {
        (void)pool.create(1);
        TALUS_CHECK(false);
    } catch (const std::runtime_error&) {
    }

    TALUS_CHECK(pool.empty());
    TALUS_CHECK(pool.capacity() == 2);

    ThrowingNode* first = pool.create(2);
    TALUS_CHECK(first->value == 2);
    TALUS_CHECK(pool.size() == 1);

    pool.destroy(first);
    TALUS_CHECK(pool.empty());
    TALUS_CHECK(ThrowingNode::destroyed == 1);

    ThrowingNode::throw_on_construct = true;
    try {
        (void)pool.create(3);
        TALUS_CHECK(false);
    } catch (const std::runtime_error&) {
    }

    TALUS_CHECK(pool.empty());
    TALUS_CHECK(ThrowingNode::destroyed == 1);

    ThrowingNode* reused = pool.create(4);
    TALUS_CHECK(reused == first);
    TALUS_CHECK(reused->value == 4);
    TALUS_CHECK(pool.size() == 1);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string{argv[1]} == "--destroy-interior-pointer") {
        run_destroy_interior_pointer_case();
        return 0;
    }

    test_create_alignment_and_growth();
    test_destroy_reuses_slots();
    test_reset_keeps_capacity();
    test_null_destroy_is_noop();
    test_destroy_rejects_interior_pointer(argv[0]);
    test_reserve_preallocates_without_construction();
    test_reserve_block_count_does_not_wrap();
    test_create_uses_reserved_blocks_before_growing();
    test_reserve_preserves_live_objects();
    test_move_semantics();
    test_interleaved_free_list_and_sequential();
    test_reset_refills_all_blocks();
    test_throwing_constructor_releases_slot();
}
