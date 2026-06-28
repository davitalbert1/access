#include "../include/tools.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace tools {
thread_local std::string current_tool_message_id = "";

static std::vector<FileChange> history;
static std::mutex history_mutex;
static int global_change_counter = 1;

// Helper to get current timestamp
static std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string list_directory(const std::string& path_str, bool recursive) {
    try {
        fs::path p(path_str);

        if (!fs::exists(p)) return R"({"e":"no_path"})";
        if (!fs::is_directory(p)) return R"({"e":"not_dir"})";

        json files = json::array();

        int count = 0;
        const int MAX_FILES = 5000;
        bool truncated = false;

        auto add_entry = [&](const fs::directory_entry& entry) {
            if (count >= MAX_FILES) {
                truncated = true;
                return;
            }

            json item;

            fs::path absolute_entry = fs::absolute(entry.path());
            item["name"] = absolute_entry.filename().generic_string();
            item["path"] = absolute_entry.generic_string();
            item["p"] = fs::relative(entry.path(), p).generic_string();
            item["d"] = entry.is_directory();
            if (!entry.is_directory()) item["s"] = entry.file_size();

            files.push_back(item);
            count++;
        };

        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                add_entry(entry);
                if (truncated) break;
            }
        } else {
            for (const auto& entry : fs::directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                add_entry(entry);
                if (truncated) break;
            }
        }

        json result;

        result["f"] = files;
        result["c"] = count;

        if (truncated) result["t"] = true;

        return result.dump();
    } catch (const std::exception& e) {
        return R"({"e":"ex"})";
    }
}

std::string search_files(const std::string& root_path_str, const std::string& pattern, bool recursive, int max_results) {
    try {
        fs::path root(root_path_str);

        if (!fs::exists(root)) return R"({"e":"no_path"})";
        if (!fs::is_directory(root)) return R"({"e":"not_dir"})";

        std::string needle = pattern;
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        json matches = json::array();
        int count = 0;
        const int MAX_RESULTS = std::max(1, max_results);

        auto add_match = [&](const fs::directory_entry& entry) {
            if (count >= MAX_RESULTS) return;

            std::string name = entry.path().filename().generic_string();
            std::string lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (!needle.empty() && lower_name.find(needle) == std::string::npos) return;

            json item;
            item["name"] = name;
            item["path"] = entry.path().generic_string();
            item["is_directory"] = entry.is_directory();
            if (!entry.is_directory()) item["size"] = entry.file_size();

            matches.push_back(item);
            count++;
        };

        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                add_match(entry);
            }
        } else {
            for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                add_match(entry);
            }
        }

        json result;
        result["matches"] = matches;
        result["count"] = count;
        return result.dump();
    } catch (...) {
        return R"({"e":"ex"})";
    }
}

std::string get_file_info(const std::string& filepath_str) {
    try {
        fs::path p(filepath_str);
        json result;

        if (!fs::exists(p)) {
            result["exists"] = false;
            return result.dump();
        }

        result["exists"] = true;
        result["name"] = p.filename().generic_string();
        result["path"] = fs::absolute(p).generic_string();
        result["parent"] = p.parent_path().generic_string();
        result["is_directory"] = fs::is_directory(p);
        result["is_file"] = fs::is_regular_file(p);
        if (fs::is_regular_file(p)) result["size"] = fs::file_size(p);

        return result.dump();
    } catch (...) {
        return R"({"e":"ex"})";
    }
}

std::string read_file(const std::string& filepath_str) {
    try {
        fs::path p(filepath_str);

        if (!fs::exists(p)) return R"({"e":"no_file"})";
        if (!fs::is_regular_file(p)) return R"({"e":"not_file"})";

        std::ifstream in(p, std::ios::in | std::ios::binary);
        if (!in.is_open()) return R"({"e":"open_fail"})";

        const size_t PREVIEW_SIZE = 24 * 1024;

        std::string content;

        content.resize(PREVIEW_SIZE);
        in.read(content.data(), PREVIEW_SIZE);

        size_t read_bytes = in.gcount();
        content.resize(read_bytes);

        std::string remaining;
        bool truncated = false;

        if (!in.eof()) truncated = true;

        json out;
        out["c"] = content;
        if (truncated) out["t"] = true;

        return out.dump();
    } catch (...) {
        return R"({"e":"ex"})";
    }
}

std::string modify_file(const std::string& filepath_str, const std::string& target_content,
    const std::string& replacement_content, std::string& err_out) {
    try {
        fs::path p(filepath_str);
        std::string abs_path = fs::absolute(p).generic_string();

        bool exists = fs::exists(p);

        std::string original;
        original.reserve(0);

        if (exists) {
            if (!fs::is_regular_file(p)) {
                err_out = "not_file";
                return R"({"e":"not_file"})";
            }

            std::ifstream in(p, std::ios::binary);
            if (!in.is_open()) {
                err_out = "open_fail";
                return R"({"e":"open_fail"})";
            }

            std::ostringstream ss;
            ss << in.rdbuf();
            original = ss.str();
        }

        std::string modified;

        if (!exists) {
            if (!target_content.empty()) {
                err_out = "bad_target_on_create";
                return R"({"e":"bad_target"})";
            }

            modified = replacement_content;
        } else {
            if (target_content.empty()) {

                if (!original.empty()) {
                    err_out = "missing_target";
                    return R"({"e":"no_target"})";
                }

                modified = replacement_content;
            } else {

                size_t pos = original.find(target_content);

                if (pos == std::string::npos) {
                    err_out = "target_not_found";
                    return R"({"e":"no_match"})";
                }

                // garante unicidade
                if (original.find(target_content, pos + 1) != std::string::npos) {
                    err_out = "not_unique";
                    return R"({"e":"multi_match"})";
                }

                modified = original;
                modified.replace(pos, target_content.size(), replacement_content);
            }
        }

        fs::create_directories(p.parent_path());

        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            err_out = "write_fail";
            return R"({"e":"write_fail"})";
        }

        out << modified;
        out.close();

        FileChange change;

        {
            std::lock_guard<std::mutex> lock(history_mutex);

            change.id = global_change_counter++;
            change.filepath = abs_path;
            change.timestamp = get_current_timestamp();
            change.original_content = std::move(original);
            change.modified_content = std::move(modified);
            change.target_content = target_content;
            change.replacement_content = replacement_content;
            change.reverted = false;
            change.message_id = current_tool_message_id;

            history.push_back(std::move(change));
        }

        return std::string("{\"ok\":") + std::to_string(change.id) + "}";

    } catch (...) {
        err_out = "exception";
        return R"({"e":"ex"})";
    }
}

bool revert_change(int change_id, std::string& err_out) {
    std::lock_guard<std::mutex> lock(history_mutex);
    for (auto& change : history) {
        if (change.id == change_id) {
            if (change.reverted) {
                err_out = "Change already reverted.";
                return false;
            }
            try {
                fs::path p(change.filepath);
                if (change.original_content.empty()) {
                    if (fs::exists(p)) fs::remove(p); // It was a new file creation, delete it
                } else {
                    // Restore original content
                    std::ofstream out(p, std::ios::out | std::ios::binary | std::ios::trunc);
                    if (!out.is_open()) {
                        err_out = "Could not open file to restore content: " + change.filepath;
                        return false;
                    }
                    out << change.original_content;
                    out.close();
                }
                change.reverted = true;
                return true;
            } catch (const std::exception& e) {
                err_out = std::string("Exception during revert: ") + e.what();
                return false;
            }
        }
    }
    err_out = "Change ID " + std::to_string(change_id) + " not found.";
    return false;
}

std::vector<FileChange> get_change_history() {
    std::lock_guard<std::mutex> lock(history_mutex);
    return history;
}

void clear_change_history() {
    std::lock_guard<std::mutex> lock(history_mutex);
    history.clear();
}
}
