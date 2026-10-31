// ComboBite chain menu data model (task 6.1).
//
// One Chain is loaded from one JSON file in data/chains/. The structs below
// mirror that file field for field; docs/data-format.md describes each field.
//
// Rules that hold everywhere:
//   - Money is always whole cents (Cents), never floating point.
//   - Calories are optional: a missing or null value means "no calorie data".
//   - Items, combos, bundles and add-ons are referred to by their "id".
//   - An empty size string means "the item's only size".
//
// This header only defines the data and its JSON conversion. Error reporting
// with line numbers belongs to the loader (task 6.3) and the data checks
// (task 6.4) build on top of it.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace combobite {

using Cents = std::int64_t;

inline constexpr int kFormatVersion = 1;

// ---------------------------------------------------------------- items ----

// One way to buy an item: its size, price and calories.
// Single-size items use the size name "regular".
struct SizeOption {
    std::string size;
    Cents price_cents = 0;
    std::optional<int> calories;  // nullopt = no calorie data
};

// Anything sold on its own: a burger, a side, a drink, a dessert.
struct Item {
    std::string id;        // unique in the chain, e.g. "big_mac"
    std::string name;      // what to say at the counter, e.g. "Big Mac"
    std::string category;  // one of Chain::categories, e.g. "burger"
    std::vector<SizeOption> sizes;
};

// --------------------------------------------------------------- add-ons ---

// An extra put on an item, such as cheese or bacon.
struct AddOn {
    std::string id;
    std::string name;
    Cents price_cents = 0;
    std::optional<int> calories;
    std::vector<std::string> applies_to_items;       // item ids
    std::vector<std::string> applies_to_categories;  // categories
    int max_per_item = 1;
};

// ------------------------------------------------- combos and bundles ------

// One thing a slot can be filled with.
struct SlotChoice {
    std::string item;       // item id
    std::string size;       // empty = the item's only size
    Cents extra_cents = 0;  // charge for choosing this instead of the default
};

// One part of a combo or bundle, e.g. "the side" or "pick 2 sandwiches".
// In a combo, the first choice is what comes with it by default.
struct Slot {
    std::string role;  // "main", "side", "drink", "pick", ...
    int count = 1;     // how many of this slot the deal includes
    std::vector<SlotChoice> choices;
};

// Making a whole combo bigger for one charge, e.g. medium -> large meal.
struct ComboUpsize {
    std::string name;       // e.g. "large"
    Cents extra_cents = 0;  // added to the combo price
    // Which slots change size, e.g. {"side": "large", "drink": "large"}.
    // Stored as a list of pairs so the JSON object order is kept.
    std::vector<std::pair<std::string, std::string>> resize;
};

// A main, side and drink (or similar) together at one price.
struct Combo {
    std::string id;
    std::string name;
    Cents price_cents = 0;
    std::string base_size;  // size name of the normal combo, e.g. "medium"; may be empty
    std::vector<Slot> slots;
    std::vector<ComboUpsize> upsizes;
};

// A deal like "any 2 for $6" or a fixed box of several items.
struct Bundle {
    std::string id;
    std::string name;
    Cents price_cents = 0;
    std::vector<Slot> slots;
};

// ---------------------------------------------------------------- chain ----

struct PricesChecked {
    std::string date;   // YYYY-MM-DD
    std::string store;  // which location the prices came from
};

struct Chain {
    int format_version = kFormatVersion;
    std::string id;    // e.g. "mcdonalds"
    std::string name;  // e.g. "McDonald's"
    std::vector<std::string> aliases;  // other names customers use
    PricesChecked prices_checked;
    std::vector<std::string> categories;
    std::vector<Item> items;
    std::vector<AddOn> add_ons;
    std::vector<Combo> combos;
    std::vector<Bundle> bundles;
    // Deals on the real menu that this format can't express, kept as notes.
    std::vector<std::string> unsupported_deals;
};

// ------------------------------------------------------- JSON conversion ---
// Required fields use j.at(), which throws nlohmann::json::out_of_range when
// a field is missing. Optional fields fall back to their defaults.

namespace detail {

inline std::optional<int> optional_int(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::nullopt;
    return it->get<int>();
}

inline void put_optional_int(nlohmann::json& j, const char* key, const std::optional<int>& v) {
    if (v) j[key] = *v; else j[key] = nullptr;
}

template <typename T>
std::vector<T> optional_list(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return {};
    return it->get<std::vector<T>>();
}

}  // namespace detail

inline void from_json(const nlohmann::json& j, SizeOption& s) {
    j.at("size").get_to(s.size);
    j.at("price_cents").get_to(s.price_cents);
    s.calories = detail::optional_int(j, "calories");
}
inline void to_json(nlohmann::json& j, const SizeOption& s) {
    j = {{"size", s.size}, {"price_cents", s.price_cents}};
    detail::put_optional_int(j, "calories", s.calories);
}

inline void from_json(const nlohmann::json& j, Item& i) {
    j.at("id").get_to(i.id);
    j.at("name").get_to(i.name);
    j.at("category").get_to(i.category);
    j.at("sizes").get_to(i.sizes);
}
inline void to_json(nlohmann::json& j, const Item& i) {
    j = {{"id", i.id}, {"name", i.name}, {"category", i.category}, {"sizes", i.sizes}};
}

inline void from_json(const nlohmann::json& j, AddOn& a) {
    j.at("id").get_to(a.id);
    j.at("name").get_to(a.name);
    j.at("price_cents").get_to(a.price_cents);
    a.calories = detail::optional_int(j, "calories");
    a.applies_to_items = detail::optional_list<std::string>(j, "applies_to_items");
    a.applies_to_categories = detail::optional_list<std::string>(j, "applies_to_categories");
    a.max_per_item = j.value("max_per_item", 1);
}
inline void to_json(nlohmann::json& j, const AddOn& a) {
    j = {{"id", a.id}, {"name", a.name}, {"price_cents", a.price_cents}};
    detail::put_optional_int(j, "calories", a.calories);
    j["applies_to_items"] = a.applies_to_items;
    j["applies_to_categories"] = a.applies_to_categories;
    j["max_per_item"] = a.max_per_item;
}

inline void from_json(const nlohmann::json& j, SlotChoice& c) {
    j.at("item").get_to(c.item);
    c.size = j.value("size", std::string{});
    c.extra_cents = j.value("extra_cents", Cents{0});
}
inline void to_json(nlohmann::json& j, const SlotChoice& c) {
    j = {{"item", c.item}};
    if (!c.size.empty()) j["size"] = c.size;
    if (c.extra_cents != 0) j["extra_cents"] = c.extra_cents;
}

inline void from_json(const nlohmann::json& j, Slot& s) {
    j.at("role").get_to(s.role);
    s.count = j.value("count", 1);
    j.at("choices").get_to(s.choices);
}
inline void to_json(nlohmann::json& j, const Slot& s) {
    j = {{"role", s.role}};
    if (s.count != 1) j["count"] = s.count;
    j["choices"] = s.choices;
}

inline void from_json(const nlohmann::json& j, ComboUpsize& u) {
    j.at("name").get_to(u.name);
    j.at("extra_cents").get_to(u.extra_cents);
    u.resize.clear();
    for (const auto& [role, size] : j.at("resize").items())
        u.resize.emplace_back(role, size.get<std::string>());
}
inline void to_json(nlohmann::json& j, const ComboUpsize& u) {
    nlohmann::json resize = nlohmann::json::object();
    for (const auto& [role, size] : u.resize) resize[role] = size;
    j = {{"name", u.name}, {"extra_cents", u.extra_cents}, {"resize", resize}};
}

inline void from_json(const nlohmann::json& j, Combo& c) {
    j.at("id").get_to(c.id);
    j.at("name").get_to(c.name);
    j.at("price_cents").get_to(c.price_cents);
    c.base_size = j.value("base_size", std::string{});
    j.at("slots").get_to(c.slots);
    c.upsizes = detail::optional_list<ComboUpsize>(j, "upsizes");
}
inline void to_json(nlohmann::json& j, const Combo& c) {
    j = {{"id", c.id}, {"name", c.name}, {"price_cents", c.price_cents}};
    if (!c.base_size.empty()) j["base_size"] = c.base_size;
    j["slots"] = c.slots;
    j["upsizes"] = c.upsizes;
}

inline void from_json(const nlohmann::json& j, Bundle& b) {
    j.at("id").get_to(b.id);
    j.at("name").get_to(b.name);
    j.at("price_cents").get_to(b.price_cents);
    j.at("slots").get_to(b.slots);
}
inline void to_json(nlohmann::json& j, const Bundle& b) {
    j = {{"id", b.id}, {"name", b.name}, {"price_cents", b.price_cents}, {"slots", b.slots}};
}

inline void from_json(const nlohmann::json& j, PricesChecked& p) {
    j.at("date").get_to(p.date);
    j.at("store").get_to(p.store);
}
inline void to_json(nlohmann::json& j, const PricesChecked& p) {
    j = {{"date", p.date}, {"store", p.store}};
}

inline void from_json(const nlohmann::json& j, Chain& c) {
    j.at("format_version").get_to(c.format_version);
    const auto& info = j.at("chain");
    info.at("id").get_to(c.id);
    info.at("name").get_to(c.name);
    c.aliases = detail::optional_list<std::string>(info, "aliases");
    j.at("prices_checked").get_to(c.prices_checked);
    j.at("categories").get_to(c.categories);
    j.at("items").get_to(c.items);
    c.add_ons = detail::optional_list<AddOn>(j, "add_ons");
    c.combos = detail::optional_list<Combo>(j, "combos");
    c.bundles = detail::optional_list<Bundle>(j, "bundles");
    c.unsupported_deals = detail::optional_list<std::string>(j, "unsupported_deals");
}
inline void to_json(nlohmann::json& j, const Chain& c) {
    j = nlohmann::json::object();
    j["format_version"] = c.format_version;
    j["chain"] = {{"id", c.id}, {"name", c.name}, {"aliases", c.aliases}};
    j["prices_checked"] = c.prices_checked;
    j["categories"] = c.categories;
    j["items"] = c.items;
    j["add_ons"] = c.add_ons;
    j["combos"] = c.combos;
    j["bundles"] = c.bundles;
    j["unsupported_deals"] = c.unsupported_deals;
}

// --------------------------------------------------------------- lookups ---

inline const Item* find_item(const Chain& chain, const std::string& id) {
    for (const auto& item : chain.items)
        if (item.id == id) return &item;
    return nullptr;
}

// Finds an item's size; an empty size name matches a single-size item.
inline const SizeOption* find_size(const Item& item, const std::string& size) {
    if (size.empty()) return item.sizes.size() == 1 ? &item.sizes.front() : nullptr;
    for (const auto& s : item.sizes)
        if (s.size == size) return &s;
    return nullptr;
}

}  // namespace combobite
