// ComboBite target format (task 2.1): what the customer wants.
//
// See docs/target-format.md. The Python front end reads and writes the same
// JSON with frontend/combobite/target.py.
//
// from_json checks the shape only and throws TargetFormatError naming the
// field (e.g. "items[1].quantity ..."). Whether the chain, items and numbers
// make sense for the menu is checked by the front end before the engine sees
// the target (task 2.4).
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "combobite/menu.hpp"

namespace combobite {

inline constexpr int kTargetFormatVersion = 1;

class TargetFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Goal { Cheapest, MostCalories };

// One thing the customer wants: exactly one of item or category is set.
struct WantedItem {
    std::optional<std::string> item;      // item id, e.g. "big_mac"
    std::optional<std::string> category;  // category, e.g. "burger"
    int quantity = 1;
    std::optional<std::string> size;      // nullopt = any size
    std::vector<std::string> add_ons;     // add-on ids
};

struct Target {
    int format_version = kTargetFormatVersion;
    std::optional<std::string> chain;  // chain id; nullopt = not named
    Goal goal = Goal::Cheapest;
    std::optional<Cents> max_price_cents;
    std::optional<int> min_calories;
    std::optional<int> max_calories;
    std::vector<WantedItem> items;
    std::vector<std::string> unsupported;
};

namespace target_detail {

[[noreturn]] inline void fail(const std::string& message) { throw TargetFormatError(message); }

template <typename T>
std::optional<T> optional_number(const nlohmann::json& j, const char* key, const std::string& where) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::nullopt;
    if (!it->is_number_integer()) fail(where + key + " must be a whole number or null");
    return it->get<T>();
}

inline std::optional<std::string> optional_string(const nlohmann::json& j, const char* key,
                                                  const std::string& where) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::nullopt;
    if (!it->is_string() || it->get<std::string>().empty())
        fail(where + key + " must be a non-empty string or null");
    return it->get<std::string>();
}

inline std::vector<std::string> string_list(const nlohmann::json& j, const char* key,
                                            const std::string& where) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return {};
    if (!it->is_array()) fail(where + key + " must be a list of non-empty strings");
    std::vector<std::string> out;
    for (const auto& v : *it) {
        if (!v.is_string() || v.get<std::string>().empty())
            fail(where + key + " must be a list of non-empty strings");
        out.push_back(v.get<std::string>());
    }
    return out;
}

inline nlohmann::json nullable(const std::optional<std::string>& v) {
    return v ? nlohmann::json(*v) : nlohmann::json(nullptr);
}

template <typename T>
nlohmann::json nullable(const std::optional<T>& v) {
    return v ? nlohmann::json(*v) : nlohmann::json(nullptr);
}

inline WantedItem read_wanted(const nlohmann::json& j, const std::string& where) {
    const std::string name = where.substr(0, where.size() - 1);  // drop trailing '.'
    if (!j.is_object()) fail(name + " must be an object");
    WantedItem w;
    w.item = optional_string(j, "item", where);
    w.category = optional_string(j, "category", where);
    if (w.item.has_value() == w.category.has_value())
        fail(name + " needs exactly one of item or category");
    auto q = j.find("quantity");
    if (q != j.end()) {
        if (!q->is_number_integer() || q->get<long long>() < 1)
            fail(where + "quantity must be a whole number of 1 or more");
        w.quantity = q->get<int>();
    }
    w.size = optional_string(j, "size", where);
    w.add_ons = string_list(j, "add_ons", where);
    return w;
}

}  // namespace target_detail

inline void from_json(const nlohmann::json& j, Target& t) {
    using namespace target_detail;
    if (!j.is_object()) fail("a target must be a JSON object");
    auto version = j.find("format_version");
    if (version == j.end() || !version->is_number_integer() ||
        version->get<int>() != kTargetFormatVersion)
        fail("format_version must be " + std::to_string(kTargetFormatVersion));
    t.format_version = kTargetFormatVersion;

    t.goal = Goal::Cheapest;
    auto goal = j.find("goal");
    if (goal != j.end() && !goal->is_null()) {
        if (*goal == "cheapest") t.goal = Goal::Cheapest;
        else if (*goal == "most_calories") t.goal = Goal::MostCalories;
        else fail("goal must be one of cheapest, most_calories");
    }

    t.chain = optional_string(j, "chain", "");
    t.max_price_cents = optional_number<Cents>(j, "max_price_cents", "");
    t.min_calories = optional_number<int>(j, "min_calories", "");
    t.max_calories = optional_number<int>(j, "max_calories", "");

    t.items.clear();
    auto items = j.find("items");
    if (items != j.end() && !items->is_null()) {
        if (!items->is_array()) fail("items must be a list");
        for (std::size_t i = 0; i < items->size(); ++i)
            t.items.push_back(read_wanted((*items)[i], "items[" + std::to_string(i) + "]."));
    }
    t.unsupported = string_list(j, "unsupported", "");
}

inline void to_json(nlohmann::json& j, const Target& t) {
    using target_detail::nullable;
    nlohmann::json items = nlohmann::json::array();
    for (const auto& w : t.items) {
        nlohmann::json e = nlohmann::json::object();
        if (w.item) e["item"] = *w.item; else e["category"] = nullable(w.category);
        e["quantity"] = w.quantity;
        e["size"] = nullable(w.size);
        e["add_ons"] = w.add_ons;
        items.push_back(std::move(e));
    }
    j = nlohmann::json::object();
    j["format_version"] = t.format_version;
    j["chain"] = nullable(t.chain);
    j["goal"] = t.goal == Goal::MostCalories ? "most_calories" : "cheapest";
    j["max_price_cents"] = nullable(t.max_price_cents);
    j["min_calories"] = nullable(t.min_calories);
    j["max_calories"] = nullable(t.max_calories);
    j["items"] = items;
    j["unsupported"] = t.unsupported;
}

}  // namespace combobite
