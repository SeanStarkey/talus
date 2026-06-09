#include <talus/talus.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Scalar = double;
using Box = talus::BoundingBox<Scalar>;
using Point = talus::Point<Scalar>;

struct DriverGeometry {
    enum class Kind {
        point,
        lat_lon,
        box,
        segment
    };

    std::size_t id = 0;
    Kind kind = Kind::point;
    std::string name;
    std::string category;
    Box box{};

    [[nodiscard]] Box bounds() const noexcept {
        return box;
    }

    bool operator==(const DriverGeometry&) const = default;
};

using Index = talus::SpatialIndex<DriverGeometry, Scalar, 9>;

struct Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::map<std::string, Json>;

struct Json {
    using Value = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

    Value value = nullptr;

    [[nodiscard]] bool is_object() const noexcept {
        return std::holds_alternative<JsonObject>(value);
    }

    [[nodiscard]] bool is_array() const noexcept {
        return std::holds_alternative<JsonArray>(value);
    }

    [[nodiscard]] const JsonObject& object() const {
        return std::get<JsonObject>(value);
    }

    [[nodiscard]] const JsonArray& array() const {
        return std::get<JsonArray>(value);
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text)
        : text_(text) {}

    [[nodiscard]] Json parse() {
        Json value = parse_value();
        skip_ws();
        if (!at_end()) {
            fail("unexpected trailing content");
        }
        return value;
    }

private:
    [[nodiscard]] Json parse_value() {
        skip_ws();
        if (at_end()) {
            fail("expected JSON value");
        }

        const char ch = peek();
        if (ch == '{') {
            return Json{parse_object()};
        }
        if (ch == '[') {
            return Json{parse_array()};
        }
        if (ch == '"') {
            return Json{parse_string()};
        }
        if (ch == 't') {
            consume_literal("true");
            return Json{true};
        }
        if (ch == 'f') {
            consume_literal("false");
            return Json{false};
        }
        if (ch == 'n') {
            consume_literal("null");
            return Json{nullptr};
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            return Json{parse_number()};
        }

        fail("expected JSON value");
    }

    [[nodiscard]] JsonObject parse_object() {
        expect('{');
        JsonObject object;
        skip_ws();
        if (consume('}')) {
            return object;
        }

        while (true) {
            skip_ws();
            if (peek() != '"') {
                fail("expected object key string");
            }
            std::string key = parse_string();
            skip_ws();
            expect(':');
            object.emplace(std::move(key), parse_value());
            skip_ws();
            if (consume('}')) {
                return object;
            }
            expect(',');
        }
    }

    [[nodiscard]] JsonArray parse_array() {
        expect('[');
        JsonArray array;
        skip_ws();
        if (consume(']')) {
            return array;
        }

        while (true) {
            array.push_back(parse_value());
            skip_ws();
            if (consume(']')) {
                return array;
            }
            expect(',');
        }
    }

    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string value;
        while (!at_end()) {
            const char ch = advance();
            if (ch == '"') {
                return value;
            }
            if (ch != '\\') {
                value.push_back(ch);
                continue;
            }

            if (at_end()) {
                fail("unterminated string escape");
            }
            const char escaped = advance();
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                fail("unsupported string escape");
            }
        }
        fail("unterminated string");
    }

    [[nodiscard]] double parse_number() {
        const std::size_t start = pos_;
        if (consume('-')) {
        }
        consume_digits();
        if (consume('.')) {
            consume_digits();
        }
        if (consume('e') || consume('E')) {
            if (!consume('+')) {
                const bool consumed_minus = consume('-');
                (void)consumed_minus;
            }
            consume_digits();
        }

        std::string number{text_.substr(start, pos_ - start)};
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || *end != '\0') {
            fail("invalid number");
        }
        return parsed;
    }

    void consume_digits() {
        if (at_end() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
            fail("expected digit");
        }
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            ++pos_;
        }
    }

    void consume_literal(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) != literal) {
            fail("expected literal");
        }
        pos_ += literal.size();
    }

    [[nodiscard]] bool consume(char expected) {
        if (!at_end() && peek() == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            std::string message = "expected '";
            message.push_back(expected);
            message.push_back('\'');
            fail(message);
        }
    }

    void skip_ws() {
        while (!at_end() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            ++pos_;
        }
    }

    [[nodiscard]] bool at_end() const noexcept {
        return pos_ >= text_.size();
    }

    [[nodiscard]] char peek() const {
        if (at_end()) {
            fail("unexpected end of input");
        }
        return text_[pos_];
    }

    [[nodiscard]] char advance() {
        const char ch = peek();
        ++pos_;
        return ch;
    }

    [[noreturn]] void fail(std::string_view message) const {
        std::ostringstream out;
        out << "JSON parse error at byte " << pos_ << ": " << message;
        throw std::runtime_error(out.str());
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

[[nodiscard]] const Json* find_member(const JsonObject& object, std::string_view key) {
    const auto it = object.find(std::string(key));
    return it == object.end() ? nullptr : &it->second;
}

[[nodiscard]] double required_number(const JsonObject& object, std::string_view key) {
    const Json* value = find_member(object, key);
    if (value == nullptr || !std::holds_alternative<double>(value->value)) {
        throw std::runtime_error("expected numeric field '" + std::string(key) + "'");
    }
    return std::get<double>(value->value);
}

[[nodiscard]] std::string optional_string(
    const JsonObject& object,
    std::string_view key,
    std::string fallback = {}) {
    const Json* value = find_member(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!std::holds_alternative<std::string>(value->value)) {
        throw std::runtime_error("expected string field '" + std::string(key) + "'");
    }
    return std::get<std::string>(value->value);
}

[[nodiscard]] std::string required_string(const JsonObject& object, std::string_view key) {
    const Json* value = find_member(object, key);
    if (value == nullptr || !std::holds_alternative<std::string>(value->value)) {
        throw std::runtime_error("expected string field '" + std::string(key) + "'");
    }
    return std::get<std::string>(value->value);
}

[[nodiscard]] Point point_from_member(const JsonObject& object, std::string_view key) {
    const Json* value = find_member(object, key);
    if (value == nullptr || !value->is_object()) {
        throw std::runtime_error("expected point object field '" + std::string(key) + "'");
    }
    const JsonObject& point = value->object();
    return {required_number(point, "x"), required_number(point, "y")};
}

[[nodiscard]] std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

[[nodiscard]] Box normalized_box(Point a, Point b) noexcept {
    return {
        {std::min(a.x, b.x), std::min(a.y, b.y)},
        {std::max(a.x, b.x), std::max(a.y, b.y)}
    };
}

[[nodiscard]] DriverGeometry geometry_from_json(const JsonObject& object, std::size_t id) {
    const std::string type = required_string(object, "type");
    DriverGeometry geometry;
    geometry.id = id;
    geometry.name = optional_string(object, "name", "record-" + std::to_string(id));
    geometry.category = optional_string(object, "category", type);

    if (type == "point") {
        const double x = required_number(object, "x");
        const double y = required_number(object, "y");
        geometry.kind = DriverGeometry::Kind::point;
        geometry.box = {{x, y}, {x, y}};
    } else if (type == "latlon" || type == "lat_lon") {
        const double lat = required_number(object, "lat");
        const double lon = required_number(object, "lon");
        geometry.kind = DriverGeometry::Kind::lat_lon;
        geometry.box = {{lon, lat}, {lon, lat}};
    } else if (type == "box" || type == "bounds") {
        const Point min{required_number(object, "min_x"), required_number(object, "min_y")};
        const Point max{required_number(object, "max_x"), required_number(object, "max_y")};
        geometry.kind = DriverGeometry::Kind::box;
        geometry.box = {min, max};
    } else if (type == "segment") {
        geometry.kind = DriverGeometry::Kind::segment;
        geometry.box = normalized_box(point_from_member(object, "start"), point_from_member(object, "end"));
    } else {
        throw std::runtime_error("unsupported geometry type '" + type + "'");
    }

    if (!geometry.box.is_valid()) {
        throw std::runtime_error("geometry '" + geometry.name + "' has invalid bounds");
    }
    return geometry;
}

[[nodiscard]] std::vector<DriverGeometry> geometries_from_json_file(const std::string& path) {
    Json root = JsonParser(read_file(path)).parse();
    const JsonArray* records = nullptr;
    if (root.is_array()) {
        records = &root.array();
    } else if (root.is_object()) {
        const Json* geometries = find_member(root.object(), "geometries");
        if (geometries == nullptr || !geometries->is_array()) {
            throw std::runtime_error("top-level object must contain a 'geometries' array");
        }
        records = &geometries->array();
    } else {
        throw std::runtime_error("JSON import must be an array or an object with a 'geometries' array");
    }

    std::vector<DriverGeometry> result;
    result.reserve(records->size());
    for (const Json& record : *records) {
        if (!record.is_object()) {
            throw std::runtime_error("each geometry record must be an object");
        }
        result.push_back(geometry_from_json(record.object(), result.size() + 1));
    }
    return result;
}

[[nodiscard]] const char* kind_name(DriverGeometry::Kind kind) noexcept {
    switch (kind) {
    case DriverGeometry::Kind::point:
        return "point";
    case DriverGeometry::Kind::lat_lon:
        return "lat/lon";
    case DriverGeometry::Kind::box:
        return "box";
    case DriverGeometry::Kind::segment:
        return "segment";
    }
    return "unknown";
}

void print_box(Box box) {
    std::cout << "[(" << box.min.x << ", " << box.min.y << "), ("
              << box.max.x << ", " << box.max.y << ")]";
}

void print_geometry(const DriverGeometry& geometry) {
    std::cout << std::setw(3) << geometry.id << "  "
              << std::left << std::setw(12) << kind_name(geometry.kind)
              << std::setw(40) << geometry.name
              << std::setw(16) << geometry.category
              << std::right;
    print_box(geometry.bounds());
    std::cout << "\n";
}

[[nodiscard]] double read_double(std::string_view prompt) {
    while (true) {
        std::cout << prompt;
        std::string line;
        if (!std::getline(std::cin, line)) {
            throw std::runtime_error("input stream closed");
        }

        char* end = nullptr;
        const double value = std::strtod(line.c_str(), &end);
        while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
            ++end;
        }
        if (end != line.c_str() && end != nullptr && *end == '\0') {
            return value;
        }
        std::cout << "Please enter a numeric value.\n";
    }
}

[[nodiscard]] std::string read_line(std::string_view prompt) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
        throw std::runtime_error("input stream closed");
    }
    return line;
}

[[nodiscard]] std::size_t read_record_id() {
    while (true) {
        const std::string line = read_line("record id: ");
        std::istringstream input(line);
        // Parse as signed so negative input is rejected here rather than
        // wrapping to a huge value, as extraction into an unsigned type would.
        long long id = 0;
        input >> id;
        const bool parsed = !input.fail();
        input >> std::ws;
        if (parsed && input.eof() && id > 0) {
            return static_cast<std::size_t>(id);
        }
        std::cout << "Please enter a positive integer id.\n";
    }
}

[[nodiscard]] Box read_query_box() {
    const double min_x = read_double("min_x: ");
    const double min_y = read_double("min_y: ");
    const double max_x = read_double("max_x: ");
    const double max_y = read_double("max_y: ");
    const Box query{{min_x, min_y}, {max_x, max_y}};
    if (!query.is_valid()) {
        throw std::runtime_error("query must satisfy min_x <= max_x and min_y <= max_y");
    }
    return query;
}

[[nodiscard]] Point read_query_point() {
    return {read_double("x / longitude: "), read_double("y / latitude: ")};
}

[[nodiscard]] bool looks_like_reversed_lat_lon(Point query) noexcept {
    return query.x >= -90.0 && query.x <= 90.0
        && (query.y < -90.0 || query.y > 90.0);
}

void import_json(Index& index, std::vector<DriverGeometry>& records) {
    const std::string path = read_line("JSON file path: ");
    std::vector<DriverGeometry> imported = geometries_from_json_file(path);
    const std::size_t first_id = records.size() + 1;
    for (std::size_t i = 0; i < imported.size(); ++i) {
        imported[i].id = first_id + i;
        index.insert(imported[i]);
        records.push_back(std::move(imported[i]));
    }
    std::cout << "Imported " << imported.size() << " geometries.\n";
}

void search_index(const Index& index) {
    const Box query = read_query_box();
    std::vector<DriverGeometry> matches = index.within(query);
    std::sort(matches.begin(), matches.end(), [](const DriverGeometry& lhs, const DriverGeometry& rhs) {
        return lhs.id < rhs.id;
    });

    std::cout << "Query ";
    print_box(query);
    std::cout << " matched " << matches.size() << " geometries.\n";
    for (const DriverGeometry& geometry : matches) {
        print_geometry(geometry);
    }
}

void radius_search(const Index& index) {
    const Point query = read_query_point();
    if (looks_like_reversed_lat_lon(query)) {
        std::cout << "Note: that looks like latitude/longitude order. Talus queries use "
                  << "x/longitude first and y/latitude second; try ("
                  << query.y << ", " << query.x << ") if this result looks wrong.\n";
    }

    const double radius = read_double("radius: ");
    if (radius < 0.0) {
        throw std::runtime_error("radius must be non-negative");
    }

    std::vector<DriverGeometry> matches = index.radius_search(query, radius);
    std::sort(matches.begin(), matches.end(), [](const DriverGeometry& lhs, const DriverGeometry& rhs) {
        return lhs.id < rhs.id;
    });

    std::cout << "Radius query centered at (" << query.x << ", " << query.y
              << ") with radius " << radius << " matched "
              << matches.size() << " geometries.\n";
    for (const DriverGeometry& geometry : matches) {
        print_geometry(geometry);
    }
}

void nearest_neighbor(const Index& index) {
    const Point query = read_query_point();
    if (looks_like_reversed_lat_lon(query)) {
        std::cout << "Note: that looks like latitude/longitude order. Talus queries use "
                  << "x/longitude first and y/latitude second; try ("
                  << query.y << ", " << query.x << ") if this result looks wrong.\n";
    }

    const std::optional<DriverGeometry> match = index.nearest_neighbor(query);

    std::cout << "Nearest stored bounds to (" << query.x << ", " << query.y << "):\n";
    if (!match) {
        std::cout << "No geometries loaded.\n";
        return;
    }

    print_geometry(*match);
    const double sq_distance = match->bounds().min_sq_distance(query);
    std::cout << "Squared distance to stored bounds: " << sq_distance << "\n";
    if (sq_distance == 0.0 && match->kind != DriverGeometry::Kind::point
        && match->kind != DriverGeometry::Kind::lat_lon) {
        std::cout << "Note: nearest_neighbor measures distance to stored bounds; "
                  << "boxes and segments can match at distance 0 when the query is inside them.\n";
    }
}

void erase_record(Index& index, std::vector<DriverGeometry>& records) {
    if (records.empty()) {
        std::cout << "No geometries loaded.\n";
        return;
    }

    const std::size_t id = read_record_id();
    const auto found = std::find_if(records.begin(), records.end(), [id](const DriverGeometry& geometry) {
        return geometry.id == id;
    });

    if (found == records.end()) {
        std::cout << "No geometry with id " << id << ".\n";
        return;
    }

    const DriverGeometry geometry = *found;
    if (!index.erase(geometry)) {
        std::cout << "Index did not contain id " << id << "; records list left unchanged.\n";
        return;
    }

    records.erase(found);
    std::cout << "Erased geometry:\n";
    print_geometry(geometry);
}

void list_records(const std::vector<DriverGeometry>& records) {
    if (records.empty()) {
        std::cout << "No geometries loaded.\n";
        return;
    }

    std::cout << " id  type        name                                    category        bounds\n";
    std::cout << "-------------------------------------------------------------------------------------------\n";
    for (const DriverGeometry& geometry : records) {
        print_geometry(geometry);
    }
}

void print_import_help() {
    std::cout
        << "JSON import accepts either an array or {\"geometries\": [...]}.\n"
        << "Supported records:\n"
        << "  {\"type\":\"point\", \"x\":-104.99, \"y\":39.74, \"name\":\"Union Station\"}\n"
        << "  {\"type\":\"latlon\", \"lat\":39.77, \"lon\":-104.96, \"name\":\"Denver Zoo\"}\n"
        << "  {\"type\":\"box\", \"min_x\":0, \"min_y\":0, \"max_x\":5, \"max_y\":5}\n"
        << "  {\"type\":\"segment\", \"start\":{\"x\":0,\"y\":0}, \"end\":{\"x\":5,\"y\":2}}\n";
}

void print_menu(const Index& index) {
    std::cout
        << "\nTalus public API driver\n"
        << "Loaded geometries: " << index.size()
        << " | empty(): " << (index.empty() ? "true" : "false") << "\n"
        << "1. List loaded geometries\n"
        << "2. Search with bounding box\n"
        << "3. Nearest neighbor from x/lon, y/lat\n"
        << "4. Radius search from x/lon, y/lat\n"
        << "5. Import JSON file\n"
        << "6. Erase geometry by id\n"
        << "7. Clear index\n"
        << "8. Show JSON import format\n"
        << "0. Quit\n"
        << "Choice: ";
}

} // namespace

int main(int argc, char** argv) {
    try {
        Index index;
        std::vector<DriverGeometry> records;

        for (int i = 1; i < argc; ++i) {
            std::vector<DriverGeometry> imported = geometries_from_json_file(argv[i]);
            const std::size_t first_id = records.size() + 1;
            for (std::size_t j = 0; j < imported.size(); ++j) {
                imported[j].id = first_id + j;
                index.insert(imported[j]);
                records.push_back(std::move(imported[j]));
            }
        }

        while (true) {
            print_menu(index);
            std::string choice;
            if (!std::getline(std::cin, choice)) {
                std::cout << "\n";
                break;
            }

            try {
                if (choice == "1") {
                    list_records(records);
                } else if (choice == "2") {
                    search_index(index);
                } else if (choice == "3") {
                    nearest_neighbor(index);
                } else if (choice == "4") {
                    radius_search(index);
                } else if (choice == "5") {
                    import_json(index, records);
                } else if (choice == "6") {
                    erase_record(index, records);
                } else if (choice == "7") {
                    index.clear();
                    records.clear();
                    std::cout << "Index cleared.\n";
                } else if (choice == "8") {
                    print_import_help();
                } else if (choice == "0" || choice == "q" || choice == "quit") {
                    break;
                } else {
                    std::cout << "Unknown menu choice.\n";
                }
            } catch (const std::exception& ex) {
                std::cout << "error: " << ex.what() << "\n";
            }
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }
}
