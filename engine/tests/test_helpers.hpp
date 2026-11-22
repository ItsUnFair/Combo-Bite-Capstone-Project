// Helpers shared by the optimizer tests (tasks 3.1 onward): loading chains,
// building targets, and reading the hand-calculated cases (task 8.1).
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "combobite/candidates.hpp"
#include "combobite/dates.hpp"
#include "combobite/loader.hpp"

namespace test {

using combobite::Chain;
using nlohmann::json;
namespace fs = std::filesystem;

inline int failures = 0;

inline void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

inline Chain load(const fs::path& file) {
    combobite::ChainReport report = combobite::load_chain_file(file, combobite::Date{2026, 11, 21});
    if (!report.loaded()) throw std::runtime_error("couldn't load " + file.string());
    return *report.chain;
}

inline combobite::Target make_target(const json& j) { return j.get<combobite::Target>(); }

inline json wanted(const std::string& item, int quantity = 1, const json& size = nullptr,
                   const json& add_ons = json::array()) {
    return {{"item", item}, {"quantity", quantity}, {"size", size}, {"add_ons", add_ons}};
}

// A target for `chain`. The items are an initializer list (not a json) so a
// list with one item stays a list.
inline json target_json(const std::string& chain, std::initializer_list<json> item_list,
                        const std::string& goal = "cheapest") {
    json items = json::array();
    for (const auto& i : item_list) items.push_back(i);
    return {{"format_version", 1}, {"chain", chain}, {"goal", goal}, {"max_price_cents", nullptr},
            {"min_calories", nullptr}, {"max_calories", nullptr}, {"items", items},
            {"unsupported", json::array()}};
}

inline const combobite::Purchase* find_by_text(const combobite::PurchaseList& list, const std::string& text) {
    for (const auto& p : list.purchases)
        if (p.describe() == text) return &p;
    return nullptr;
}

// Order lines in a fixed order, so two orders can be compared.
inline json canonical(json order) {
    for (auto& line : order)
        if (line.contains("contents")) {
            std::vector<std::string> parts;
            for (const auto& c : line["contents"]) parts.push_back(c.dump());
            std::sort(parts.begin(), parts.end());
            line["contents"] = json::array();
            for (const auto& p : parts) line["contents"].push_back(json::parse(p));
        }
    std::vector<std::string> lines;
    for (const auto& line : order) lines.push_back(line.dump());
    std::sort(lines.begin(), lines.end());
    json out = json::array();
    for (const auto& l : lines) out.push_back(json::parse(l));
    return out;
}

// tests/optimizer_cases.json and every chain its cases use.
struct OptimizerCases {
    json doc;
    std::map<std::string, Chain> chains;
};

inline OptimizerCases load_optimizer_cases(const fs::path& project) {
    OptimizerCases cases;
    std::ifstream in(project / "tests" / "optimizer_cases.json");
    cases.doc = json::parse(in);
    for (const auto& folder : cases.doc.at("data_folders"))
        for (const auto& entry : fs::directory_iterator(project / folder.get<std::string>()))
            if (entry.path().extension() == ".json") {
                Chain c = load(entry.path());
                cases.chains[c.id] = c;
            }
    return cases;
}

// The things in an order written as JSON lines (the cases' format), found in
// `list`. Add-ons are ignored here; Coverage::covers places them. Returns
// false, with `missing` set, when a line isn't a listed thing to order.
inline bool order_things(const combobite::PurchaseList& list, const json& order,
                         std::vector<const combobite::Purchase*>& things, std::string& missing) {
    std::map<std::string, const combobite::Purchase*> by_line;
    for (const auto& p : list.purchases) {
        combobite::Candidate one{{&p}, {}};
        by_line[canonical(combobite::candidate_to_json(one))[0].dump()] = &p;
    }
    things.clear();
    for (json line : order) {
        const int quantity = line.at("quantity");
        line["quantity"] = 1;
        line.erase("add_ons");
        if (line.contains("contents"))
            for (auto& part : line["contents"]) part.erase("add_ons");
        auto it = by_line.find(canonical(json::array({line}))[0].dump());
        if (it == by_line.end()) {
            missing = line.dump();
            return false;
        }
        for (int q = 0; q < quantity; ++q) things.push_back(it->second);
    }
    return true;
}

}  // namespace test
