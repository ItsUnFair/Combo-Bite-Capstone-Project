#include "combobite/loader.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>

#include "combobite/checks.hpp"

namespace combobite {

namespace {

using nlohmann::json;

// ------------------------------------------------------------- shape checks --
// Walks the JSON before it's converted to structs, so every missing field or
// wrong type is reported with the entry it belongs to.
class ShapeChecker {
public:
    explicit ShapeChecker(std::vector<Issue>& issues) : issues_(issues) {}

    void check_chain_file(const json& root) {
        if (!root.is_object()) {
            error("file", "must be a JSON object { ... } at the top level");
            return;
        }
        known(root, "file", {"format_version", "chain", "prices_checked", "categories", "items",
                             "add_ons", "combos", "bundles", "unsupported_deals"});
        integer(root, "format_version", "file");
        if (const json* chain = object(root, "chain", "file", true)) {
            known(*chain, "chain", {"id", "name", "aliases"});
            text(*chain, "id", "chain");
            text(*chain, "name", "chain");
            text_list(*chain, "aliases", "chain", false);
        }
        if (const json* prices = object(root, "prices_checked", "file", true)) {
            known(*prices, "prices_checked", {"date", "store"});
            text(*prices, "date", "prices_checked");
            text(*prices, "store", "prices_checked");
        }
        text_list(root, "categories", "file", true);

        each(root, "items", "item", true, [this](const json& item, const std::string& entry) {
            known(item, entry, {"id", "name", "category", "sizes"});
            text(item, "name", entry);
            text(item, "category", entry);
            if (const json* sizes = list(item, "sizes", entry, true)) {
                for (std::size_t i = 0; i < sizes->size(); ++i) {
                    const json& size = (*sizes)[i];
                    std::string where = entry + ", size " + std::to_string(i + 1);
                    if (size.is_object() && size.contains("size") && size["size"].is_string())
                        where = entry + ", size '" + size["size"].get<std::string>() + "'";
                    if (!size.is_object()) {
                        error(where, "must be an object { \"size\": ..., \"price_cents\": ... }");
                        continue;
                    }
                    known(size, where, {"size", "price_cents", "calories"});
                    text(size, "size", where);
                    cents(size, "price_cents", where, true);
                    integer_or_null(size, "calories", where);
                }
            }
        });

        each(root, "add_ons", "add-on", false, [this](const json& add_on, const std::string& entry) {
            known(add_on, entry, {"id", "name", "price_cents", "calories", "applies_to_items",
                                  "applies_to_categories", "max_per_item"});
            text(add_on, "name", entry);
            cents(add_on, "price_cents", entry, true);
            integer_or_null(add_on, "calories", entry);
            text_list(add_on, "applies_to_items", entry, false);
            text_list(add_on, "applies_to_categories", entry, false);
            integer(add_on, "max_per_item", entry, false);
        });

        each(root, "combos", "combo", false, [this](const json& combo, const std::string& entry) {
            known(combo, entry, {"id", "name", "price_cents", "base_size", "slots", "upsizes"});
            text(combo, "name", entry);
            cents(combo, "price_cents", entry, true);
            text(combo, "base_size", entry, false);
            slots(combo, entry);
            if (const json* upsizes = list(combo, "upsizes", entry, false)) {
                for (std::size_t i = 0; i < upsizes->size(); ++i) {
                    const json& upsize = (*upsizes)[i];
                    const std::string where = entry + ", upsize " + std::to_string(i + 1);
                    if (!upsize.is_object()) {
                        error(where, "must be an object { \"name\": ..., \"extra_cents\": ..., \"resize\": ... }");
                        continue;
                    }
                    known(upsize, where, {"name", "extra_cents", "resize"});
                    text(upsize, "name", where);
                    cents(upsize, "extra_cents", where, true);
                    if (const json* resize = object(upsize, "resize", where, true)) {
                        for (const auto& [role, size] : resize->items())
                            if (!size.is_string())
                                error(where, "\"resize\" value for '" + role + "' must be a size name");
                    }
                }
            }
        });

        each(root, "bundles", "bundle", false, [this](const json& bundle, const std::string& entry) {
            known(bundle, entry, {"id", "name", "price_cents", "slots"});
            text(bundle, "name", entry);
            cents(bundle, "price_cents", entry, true);
            slots(bundle, entry);
        });

        text_list(root, "unsupported_deals", "file", false);
    }

private:
    std::vector<Issue>& issues_;

    void error(const std::string& entry, const std::string& message) {
        issues_.push_back({Severity::Error, entry, message});
    }

    // Warns about field names this format doesn't use, which are usually typos
    // ("upsize" for "upsizes") and would otherwise be ignored without a word.
    void known(const json& obj, const std::string& entry, std::initializer_list<const char*> keys) {
        for (const auto& [key, value] : obj.items()) {
            (void)value;
            bool found = false;
            for (const char* k : keys) found = found || key == k;
            if (!found)
                issues_.push_back({Severity::Warning, entry, "unknown field \"" + key + "\" is ignored (a typo?)"});
        }
    }

    // Finds a field. Reports it when required and missing (or null).
    const json* find(const json& obj, const char* key, const std::string& entry, bool required,
                     const std::string& missing_message = "") {
        auto it = obj.find(key);
        if (it == obj.end() || it->is_null()) {
            if (required)
                error(entry, missing_message.empty() ? std::string("is missing \"") + key + "\"" : missing_message);
            return nullptr;
        }
        return &*it;
    }

    void text(const json& obj, const char* key, const std::string& entry, bool required = true) {
        const json* value = find(obj, key, entry, required);
        if (value && (!value->is_string() || value->get<std::string>().empty()))
            error(entry, std::string("\"") + key + "\" must be text, not " + describe(*value));
    }

    void integer(const json& obj, const char* key, const std::string& entry, bool required = true) {
        const json* value = find(obj, key, entry, required);
        if (value && !value->is_number_integer())
            error(entry, std::string("\"") + key + "\" must be a whole number, not " + describe(*value));
    }

    void integer_or_null(const json& obj, const char* key, const std::string& entry) {
        auto it = obj.find(key);
        if (it != obj.end() && !it->is_null() && !it->is_number_integer())
            error(entry, std::string("\"") + key + "\" must be a whole number or null, not " + describe(*it));
    }

    void cents(const json& obj, const char* key, const std::string& entry, bool required) {
        const std::string missing = std::string(key) == "price_cents"
                                        ? "has no price (\"price_cents\" is missing)"
                                        : std::string("is missing \"") + key + "\"";
        const json* value = find(obj, key, entry, required, missing);
        if (!value || value->is_number_integer()) return;
        std::string message = std::string("\"") + key + "\" must be a whole number of cents, not " + describe(*value);
        if (value->is_number()) {
            const double dollars = value->get<double>();
            std::ostringstream hint;
            hint << " (write " << value->dump() << " as " << std::llround(dollars * 100) << ")";
            message += hint.str();
        }
        error(entry, message);
    }

    void text_list(const json& obj, const char* key, const std::string& entry, bool required) {
        const json* value = find(obj, key, entry, required);
        if (!value) return;
        bool ok = value->is_array();
        if (ok)
            for (const auto& v : *value)
                if (!v.is_string() || v.get<std::string>().empty()) ok = false;
        if (!ok) error(entry, std::string("\"") + key + "\" must be a list of text, like [\"a\", \"b\"]");
    }

    const json* list(const json& obj, const char* key, const std::string& entry, bool required) {
        const json* value = find(obj, key, entry, required);
        if (value && !value->is_array()) {
            error(entry, std::string("\"") + key + "\" must be a list [ ... ], not " + describe(*value));
            return nullptr;
        }
        return value;
    }

    const json* object(const json& obj, const char* key, const std::string& entry, bool required) {
        const json* value = find(obj, key, entry, required);
        if (value && !value->is_object()) {
            error(entry, std::string("\"") + key + "\" must be an object { ... }, not " + describe(*value));
            return nullptr;
        }
        return value;
    }

    // Runs `check` on each entry of a list of objects that have an "id".
    template <typename Check>
    void each(const json& root, const char* key, const char* kind, bool required, Check check) {
        const json* entries = list(root, key, "file", required);
        if (!entries) return;
        for (std::size_t i = 0; i < entries->size(); ++i) {
            const json& e = (*entries)[i];
            std::string entry = std::string(key) + "[" + std::to_string(i) + "]";
            if (e.is_object() && e.contains("id") && e["id"].is_string() && !e["id"].get<std::string>().empty())
                entry = std::string(kind) + " '" + e["id"].get<std::string>() + "'";
            if (!e.is_object()) {
                error(entry, "must be an object { ... }");
                continue;
            }
            text(e, "id", entry);
            check(e, entry);
        }
    }

    void slots(const json& deal, const std::string& entry) {
        const json* slot_list = list(deal, "slots", entry, true);
        if (!slot_list) return;
        for (std::size_t i = 0; i < slot_list->size(); ++i) {
            const json& slot = (*slot_list)[i];
            std::string where = entry + ", slot " + std::to_string(i + 1);
            if (slot.is_object() && slot.contains("role") && slot["role"].is_string())
                where = entry + ", slot '" + slot["role"].get<std::string>() + "'";
            if (!slot.is_object()) {
                error(where, "must be an object { \"role\": ..., \"choices\": [ ... ] }");
                continue;
            }
            known(slot, where, {"role", "count", "choices"});
            text(slot, "role", where);
            integer(slot, "count", where, false);
            if (const json* choices = list(slot, "choices", where, true)) {
                for (std::size_t c = 0; c < choices->size(); ++c) {
                    const json& choice = (*choices)[c];
                    const std::string at = where + ", choice " + std::to_string(c + 1);
                    if (!choice.is_object()) {
                        error(at, "must be an object { \"item\": ... }");
                        continue;
                    }
                    known(choice, at, {"item", "size", "extra_cents"});
                    text(choice, "item", at);
                    text(choice, "size", at, false);
                    cents(choice, "extra_cents", at, false);
                }
            }
        }
    }

    static std::string describe(const json& value) {
        switch (value.type()) {
            case json::value_t::string: return value.get<std::string>().empty() ? "empty text" : "text " + value.dump();
            case json::value_t::number_float: return "the decimal " + value.dump();
            case json::value_t::number_integer:
            case json::value_t::number_unsigned: return "the number " + value.dump();
            case json::value_t::boolean: return value.dump();
            case json::value_t::array: return "a list";
            case json::value_t::object: return "an object";
            case json::value_t::null: return "null";
            default: return "an unexpected value";
        }
    }
};

// "[json.exception.parse_error.101] parse error at line 3, column 5: syntax error ..."
// becomes "line 3, column 5: syntax error ...".
std::string friendly_parse_error(const std::string& what) {
    const std::string marker = "parse error at ";
    const auto at = what.find(marker);
    std::string message = at == std::string::npos ? what : what.substr(at + marker.size());
    return message;
}

}  // namespace

int ChainReport::count(Severity severity) const {
    return static_cast<int>(std::count_if(issues.begin(), issues.end(),
                                          [severity](const Issue& i) { return i.severity == severity; }));
}

ChainReport load_chain_text(const std::string& text, const std::string& file_name, const Date& today) {
    ChainReport report;
    report.file = file_name;

    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& e) {
        report.issues.push_back({Severity::Error, "file", friendly_parse_error(e.what())});
        return report;
    }

    if (root.is_object() && root.contains("chain") && root["chain"].is_object() &&
        root["chain"].contains("name") && root["chain"]["name"].is_string())
        report.chain_name = root["chain"]["name"].get<std::string>();

    ShapeChecker(report.issues).check_chain_file(root);
    if (report.count(Severity::Error) > 0) return report;

    Chain chain = root.get<Chain>();
    for (auto& issue : check_chain(chain, today)) report.issues.push_back(std::move(issue));
    if (report.count(Severity::Error) == 0) report.chain = std::move(chain);
    return report;
}

ChainReport load_chain_file(const std::filesystem::path& path, const Date& today) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ChainReport report;
        report.file = path.filename().string();
        report.issues.push_back({Severity::Error, "file", "can't be opened: " + path.string()});
        return report;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return load_chain_text(text.str(), path.filename().string(), today);
}

std::vector<const Chain*> DataFolderReport::loaded_chains() const {
    std::vector<const Chain*> chains;
    for (const auto& file : files)
        if (file.chain) chains.push_back(&*file.chain);
    return chains;
}

int DataFolderReport::count(Severity severity) const {
    int total = 0;
    for (const auto& file : files) total += file.count(severity);
    return total;
}

DataFolderReport load_data_folder(const std::filesystem::path& folder, const Date& today) {
    namespace fs = std::filesystem;
    DataFolderReport report;
    report.folder = folder;

    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        report.fatal = "The data folder wasn't found. Looked in: " + fs::absolute(folder, ec).string();
        return report;
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(folder, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json") paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    if (paths.empty()) {
        report.fatal = "No chain files (*.json) were found in: " + fs::absolute(folder, ec).string();
        return report;
    }

    std::set<std::string> seen_ids;
    for (const auto& path : paths) {
        ChainReport chain_report = load_chain_file(path, today);
        if (chain_report.chain) {
            const std::string& id = chain_report.chain->id;
            if (!seen_ids.insert(id).second) {
                std::string other;
                for (const auto& earlier : report.files)
                    if (earlier.chain && earlier.chain->id == id) other = earlier.file;
                chain_report.issues.push_back(
                    {Severity::Error, "chain", "id '" + id + "' is already used by " + other});
                chain_report.chain.reset();
            }
        }
        report.files.push_back(std::move(chain_report));
    }
    return report;
}

}  // namespace combobite
