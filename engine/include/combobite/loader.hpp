// Loading chain data files (task 6.3) and checking them (task 6.4).
//
// A chain file goes through three steps, and every problem found becomes an
// Issue instead of stopping at the first one:
//   1. Read the JSON. A syntax error is reported with its line and column.
//   2. Check the shape: every field present with the right type. Problems
//      are reported by entry, e.g. "item 'fries', size 'small'".
//   3. Check the meaning (check_chain in checks.hpp): undefined items,
//      duplicate names, zero prices, old prices, and so on.
// Any error keeps the chain from loading; warnings don't.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "combobite/dates.hpp"
#include "combobite/menu.hpp"

namespace combobite {

enum class Severity { Error, Warning };

struct Issue {
    Severity severity = Severity::Error;
    std::string entry;    // where: "file", "item 'fries'", "combo 'big_mac_meal', slot 'side'"
    std::string message;  // what's wrong, in plain words
};

// The result of loading one chain file.
struct ChainReport {
    std::string file;            // file name, e.g. "wendys.json"
    std::string chain_name;      // e.g. "Wendy's", when the file says
    std::optional<Chain> chain;  // set only when the file has no errors
    std::vector<Issue> issues;

    bool loaded() const { return chain.has_value(); }
    int count(Severity severity) const;
};

ChainReport load_chain_text(const std::string& text, const std::string& file_name, const Date& today);
ChainReport load_chain_file(const std::filesystem::path& path, const Date& today);

// The result of loading every chain file in the data folder.
struct DataFolderReport {
    std::filesystem::path folder;
    std::string fatal;  // set when the folder is missing or has no chain files
    std::vector<ChainReport> files;

    std::vector<const Chain*> loaded_chains() const;
    int count(Severity severity) const;
};

// Loads every *.json file in the folder (not subfolders), in name order.
// Two files with the same chain id: the second one doesn't load.
DataFolderReport load_data_folder(const std::filesystem::path& folder, const Date& today);

}  // namespace combobite
