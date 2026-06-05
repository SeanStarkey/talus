#include <talus/concepts.hpp>
#include <talus/detail/algorithms.hpp>
#include <talus/detail/node.hpp>
#include <talus/detail/pool_alloc.hpp>
#include <talus/geometry.hpp>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Place {
    double x = 0.0;
    double y = 0.0;
    std::string name;
    std::string category;
};

using Scalar = double;
using Box = talus::BoundingBox<Scalar>;
using Node = talus::detail::RTreeNode<Place, Scalar, 4>;

class ExampleRTree {
public:
    void insert(Place place) {
        const Box bounds = talus::bounding_box_of<Scalar>(place);
        const auto result = talus::detail::insert_with_split(root_, pool_, bounds, std::move(place));
        if (!result.inserted) {
            throw std::runtime_error("R*-tree insertion failed");
        }
        ++size_;
        split_count_ += result.split_count;
        if (result.grew_height) {
            ++root_growth_count_;
        }
    }

    [[nodiscard]] std::vector<Place> search(Box query) const {
        std::vector<Place> matches;
        talus::detail::search(root_, query, [&](const Place& place) {
            matches.push_back(place);
        });

        std::sort(matches.begin(), matches.end(), [](const Place& lhs, const Place& rhs) {
            return lhs.name < rhs.name;
        });
        return matches;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t split_count() const noexcept {
        return split_count_;
    }

    [[nodiscard]] std::size_t root_growth_count() const noexcept {
        return root_growth_count_;
    }

    [[nodiscard]] std::size_t allocated_nodes() const noexcept {
        return pool_.size() + 1;
    }

    [[nodiscard]] bool root_is_internal() const noexcept {
        return root_.is_internal();
    }

private:
    Node root_{};
    talus::detail::PoolAllocator<Node, 16> pool_{};
    std::size_t size_ = 0;
    std::size_t split_count_ = 0;
    std::size_t root_growth_count_ = 0;
};

[[nodiscard]] double parse_double(std::string_view text) {
    std::string value{text};
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        throw std::invalid_argument("expected a numeric query coordinate");
    }
    return parsed;
}

[[nodiscard]] Box query_from_args(int argc, char** argv) {
    if (argc == 1) {
        return {{-105.02, 39.70}, {-104.95, 39.77}};
    }
    if (argc != 5) {
        throw std::invalid_argument("usage: talus_rstar_driver [min_x min_y max_x max_y]");
    }

    Box query{
        {parse_double(argv[1]), parse_double(argv[2])},
        {parse_double(argv[3]), parse_double(argv[4])}
    };
    if (!query.is_valid()) {
        throw std::invalid_argument("query must satisfy min_x <= max_x and min_y <= max_y");
    }
    return query;
}

void print_box(Box box) {
    std::cout << "[(" << box.min.x << ", " << box.min.y << "), ("
              << box.max.x << ", " << box.max.y << ")]";
}

} // namespace

int main(int argc, char** argv) {
    try {
        ExampleRTree index;
        const std::vector<Place> places{
            {-104.9903, 39.7392, "Union Station", "transit"},
            {-104.9952, 39.7420, "Museum of Contemporary Art", "museum"},
            {-104.9849, 39.7462, "Coors Field", "stadium"},
            {-104.9876, 39.7400, "16th Street Mall", "shopping"},
            {-104.9887, 39.7528, "RiNo Art District", "arts"},
            {-104.9623, 39.7692, "Denver Zoo", "park"},
            {-104.9518, 39.7475, "Denver Museum of Nature and Science", "museum"},
            {-104.9990, 39.7405, "Ball Arena", "stadium"},
            {-105.0178, 39.7473, "Empower Field", "stadium"},
            {-104.9564, 39.7203, "Cherry Creek", "shopping"},
            {-105.0440, 39.7555, "Sloan's Lake", "park"},
            {-104.9837, 39.7319, "Colorado State Capitol", "civic"}
        };

        for (Place place : places) {
            index.insert(std::move(place));
        }

        const Box query = query_from_args(argc, argv);
        const std::vector<Place> matches = index.search(query);

        std::cout << "Talus R*-tree example driver\n";
        std::cout << "Inserted " << index.size() << " Denver places with MaxChildren=4\n";
        std::cout << "Node splits: " << index.split_count()
                  << ", root growths: " << index.root_growth_count()
                  << ", allocated nodes: " << index.allocated_nodes()
                  << ", root is " << (index.root_is_internal() ? "internal" : "leaf") << "\n";

        std::cout << "Query ";
        print_box(query);
        std::cout << " matched " << matches.size() << " place";
        if (matches.size() != 1) {
            std::cout << "s";
        }
        std::cout << ":\n";

        for (const Place& place : matches) {
            std::cout << "  - " << std::left << std::setw(38) << place.name
                      << " (" << place.category << ") at "
                      << place.x << ", " << place.y << "\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
