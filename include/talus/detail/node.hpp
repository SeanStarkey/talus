#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "../geometry.hpp"

namespace talus::detail {

template<typename T, typename Scalar = double, std::size_t MaxChildren = 9>
class RTreeNode;

template<typename T, typename Scalar>
struct RTreeValueEntry {
    BoundingBox<Scalar> bounds{};
    T value;

    template<typename... Args>
    constexpr RTreeValueEntry(BoundingBox<Scalar> entry_bounds, Args&&... args)
        : bounds(entry_bounds),
          value(std::forward<Args>(args)...) {}
};

template<typename T, typename Scalar = double, std::size_t MaxChildren = 9>
struct RTreeChildEntry {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    BoundingBox<Scalar> bounds{};
    node_type* child = nullptr;

    constexpr RTreeChildEntry(BoundingBox<Scalar> entry_bounds, node_type* entry_child) noexcept
        : bounds(entry_bounds),
          child(entry_child) {}
};

template<typename T, typename Scalar, std::size_t MaxChildren>
class alignas(64) RTreeNode {
    static_assert(MaxChildren >= 4, "RTreeNode requires MaxChildren >= 4");

public:
    using value_type = T;
    using scalar_type = Scalar;
    using node_type = RTreeNode<T, Scalar, MaxChildren>;
    using value_entry_type = RTreeValueEntry<T, Scalar>;
    using child_entry_type = RTreeChildEntry<T, Scalar, MaxChildren>;

    static constexpr std::size_t max_children = MaxChildren;
    static constexpr std::size_t min_children = MaxChildren / 2;
    static constexpr std::size_t entry_capacity = MaxChildren + 1;

    explicit constexpr RTreeNode(bool leaf = true) noexcept
        : is_leaf_(leaf) {}

    RTreeNode(const RTreeNode&) = delete;
    RTreeNode& operator=(const RTreeNode&) = delete;

    RTreeNode(RTreeNode&&) = delete;
    RTreeNode& operator=(RTreeNode&&) = delete;

    ~RTreeNode() {
        clear();
    }

    [[nodiscard]] constexpr bool is_leaf() const noexcept {
        return is_leaf_;
    }

    [[nodiscard]] constexpr bool is_internal() const noexcept {
        return !is_leaf_;
    }

    [[nodiscard]] constexpr std::size_t count() const noexcept {
        return count_;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return count_ == 0;
    }

    [[nodiscard]] constexpr bool full() const noexcept {
        return count_ >= max_children;
    }

    [[nodiscard]] constexpr bool has_overflow() const noexcept {
        return count_ > max_children;
    }

    [[nodiscard]] constexpr bool can_append_entry() const noexcept {
        return count_ < entry_capacity;
    }

    [[nodiscard]] constexpr bool underfull() const noexcept {
        return count_ < min_children;
    }

    [[nodiscard]] constexpr BoundingBox<Scalar> bounds() const noexcept {
        return bounds_;
    }

    [[nodiscard]] constexpr node_type* parent() noexcept {
        return parent_;
    }

    [[nodiscard]] constexpr const node_type* parent() const noexcept {
        return parent_;
    }

    constexpr void set_parent(node_type* parent) noexcept {
        parent_ = parent;
    }

    template<typename U>
    value_entry_type& append_value(BoundingBox<Scalar> entry_bounds, U&& value) {
        return emplace_value(entry_bounds, std::forward<U>(value));
    }

    template<typename... Args>
    value_entry_type& emplace_value(BoundingBox<Scalar> entry_bounds, Args&&... args) {
        assert(is_leaf_);
        assert(can_append_entry());

        value_entry_type* entry = value_entry(count_);
        std::construct_at(entry, entry_bounds, std::forward<Args>(args)...);
        append_bounds(entry_bounds);
        ++count_;
        return *entry;
    }

    child_entry_type& append_child(BoundingBox<Scalar> entry_bounds, node_type* child) {
        assert(!is_leaf_);
        assert(child != nullptr);
        assert(can_append_entry());

        child_entry_type* entry = child_entry(count_);
        std::construct_at(entry, entry_bounds, child);
        child->set_parent(this);
        append_bounds(entry_bounds);
        ++count_;
        return *entry;
    }

    [[nodiscard]] value_entry_type& value_at(std::size_t index) noexcept {
        assert(is_leaf_);
        assert(index < count_);
        return *value_entry(index);
    }

    [[nodiscard]] const value_entry_type& value_at(std::size_t index) const noexcept {
        assert(is_leaf_);
        assert(index < count_);
        return *value_entry(index);
    }

    [[nodiscard]] child_entry_type& child_at(std::size_t index) noexcept {
        assert(!is_leaf_);
        assert(index < count_);
        return *child_entry(index);
    }

    [[nodiscard]] const child_entry_type& child_at(std::size_t index) const noexcept {
        assert(!is_leaf_);
        assert(index < count_);
        return *child_entry(index);
    }

    void remove_at(std::size_t index) {
        assert(index < count_);

        const std::size_t last = count_ - 1;

        if (is_leaf_) {
            std::destroy_at(value_entry(index));
            if (index != last) {
                std::construct_at(value_entry(index), std::move(*value_entry(last)));
                std::destroy_at(value_entry(last));
            }
        } else {
            child_entry(index)->child->set_parent(nullptr);
            std::destroy_at(child_entry(index));
            if (index != last) {
                std::construct_at(child_entry(index), std::move(*child_entry(last)));
                std::destroy_at(child_entry(last));
            }
        }

        --count_;
        recompute_bounds();
    }

    void update_bounds(std::size_t index, BoundingBox<Scalar> entry_bounds) noexcept {
        assert(index < count_);

        if (is_leaf_) {
            value_entry(index)->bounds = entry_bounds;
        } else {
            child_entry(index)->bounds = entry_bounds;
        }

        recompute_bounds();
    }

    void recompute_bounds() noexcept {
        if (count_ == 0) {
            bounds_ = {};
            return;
        }

        BoundingBox<Scalar> combined = entry_bounds_at(0);
        for (std::size_t i = 1; i < count_; ++i) {
            combined = combined.expand(entry_bounds_at(i));
        }
        bounds_ = combined;
    }

    void clear() noexcept(std::is_nothrow_destructible_v<T>) {
        if (is_leaf_) {
            for (std::size_t i = 0; i < count_; ++i) {
                std::destroy_at(value_entry(i));
            }
        } else {
            for (std::size_t i = 0; i < count_; ++i) {
                child_entry(i)->child->set_parent(nullptr);
                std::destroy_at(child_entry(i));
            }
        }

        count_ = 0;
        bounds_ = {};
    }

    void reset_as_leaf() noexcept(std::is_nothrow_destructible_v<T>) {
        clear();
        is_leaf_ = true;
        parent_ = nullptr;
    }

    void reset_as_internal() noexcept(std::is_nothrow_destructible_v<T>) {
        clear();
        is_leaf_ = false;
        parent_ = nullptr;
    }

private:
    using value_storage_type = std::aligned_storage_t<sizeof(value_entry_type), alignof(value_entry_type)>;
    using child_storage_type = std::aligned_storage_t<sizeof(child_entry_type), alignof(child_entry_type)>;

    union EntryStorage {
        constexpr EntryStorage() noexcept {}
        ~EntryStorage() {}

        value_storage_type values[entry_capacity];
        child_storage_type children[entry_capacity];
    };

    [[nodiscard]] value_entry_type* value_entry(std::size_t index) noexcept {
        return std::launder(reinterpret_cast<value_entry_type*>(&storage_.values[index]));
    }

    [[nodiscard]] const value_entry_type* value_entry(std::size_t index) const noexcept {
        return std::launder(reinterpret_cast<const value_entry_type*>(&storage_.values[index]));
    }

    [[nodiscard]] child_entry_type* child_entry(std::size_t index) noexcept {
        return std::launder(reinterpret_cast<child_entry_type*>(&storage_.children[index]));
    }

    [[nodiscard]] const child_entry_type* child_entry(std::size_t index) const noexcept {
        return std::launder(reinterpret_cast<const child_entry_type*>(&storage_.children[index]));
    }

    [[nodiscard]] BoundingBox<Scalar> entry_bounds_at(std::size_t index) const noexcept {
        return is_leaf_ ? value_entry(index)->bounds : child_entry(index)->bounds;
    }

    constexpr void append_bounds(BoundingBox<Scalar> entry_bounds) noexcept {
        bounds_ = count_ == 0 ? entry_bounds : bounds_.expand(entry_bounds);
    }

    bool is_leaf_ = true;
    std::size_t count_ = 0;
    BoundingBox<Scalar> bounds_{};
    node_type* parent_ = nullptr;
    EntryStorage storage_{};
};

static_assert(alignof(RTreeNode<int>) == 64);

} // namespace talus::detail
