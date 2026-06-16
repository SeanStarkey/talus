#include <talus/talus.hpp>

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

struct Site {
    double x;
    double y;
    int id;
};

} // namespace

int main() {
    static_assert(TALUS_VERSION_MAJOR == 1);
    static_assert(TALUS_VERSION_MINOR == 0);
    static_assert(TALUS_VERSION_PATCH == 0);
    static_assert(TALUS_VERSION == 10000);

    talus::SpatialIndex<Site> index;
    index.insert(Site{0.0, 0.0, 1});
    index.insert(Site{4.0, 4.0, 2});
    index.insert(Site{8.0, 8.0, 3});

    const auto results = index.search(talus::BoundingBox<double>{{-1.0, -1.0}, {5.0, 5.0}});
    std::vector<int> ids;
    ids.reserve(results.size());
    for(const Site& site : results) {
        ids.push_back(site.id);
    }
    std::sort(ids.begin(), ids.end());

    if(ids != std::vector<int>{1, 2}) {
        std::cerr << "unexpected Talus consumer smoke-test result count=" << ids.size() << '\n';
        return 1;
    }

    return 0;
}
