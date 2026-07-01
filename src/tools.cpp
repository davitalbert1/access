#include "../include/tools.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <iterator>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const std::unordered_set<std::string> DEFAULT_EXCLUDED = {".git", ".vs", ".vscode",
    ".idea", "node_modules", "bin", "obj"};

int hidden_count = 0;

namespace tools {
    thread_local std::string current_tool_message_id = "";

    static std::vector<FileChange> history;
    static std::mutex history_mutex;
    static int global_change_counter = 1;

    static std::vector<std::string> split_into_chunks(const std::string& text, size_t chunk_size) {
        std::vector<std::string> chunks;
        size_t pos = 0;

        while (pos < text.size()) {
            size_t len = std::min(chunk_size, text.size() - pos);
            chunks.push_back(text.substr(pos, len));
            pos += len;
        }

        return chunks;
    }

    // Helper to get current timestamp
    static std::string get_current_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string list_directory(const std::string& path_str, bool recursive, bool show_excluded, int max_depth) {
        hidden_count = 0;

        try {
            fs::path p(path_str);

            if (!fs::exists(p)) return R"({"e":"no_path"})";
            if (!fs::is_directory(p)) return R"({"e":"not_dir"})";

            json files = json::array();

            int count = 0;
            const int MAX_FILES = 5000;
            bool truncated = false;

            auto add_entry = [&](const fs::directory_entry& entry) {
                std::string filename = entry.path().filename().generic_string();
                if (!show_excluded && DEFAULT_EXCLUDED.find(filename) != DEFAULT_EXCLUDED.end()) {
                    hidden_count++;
                    return;
                }

                if (count >= MAX_FILES) {
                    truncated = true;
                    return;
                }

                auto rel = fs::relative(entry.path(), p).generic_string();
                if (entry.is_directory()) files.push_back(rel + "/");
                else files.push_back(rel);
                count++;
            };

            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                    auto rel = fs::relative(entry.path(), p);
                    int depth = std::distance(rel.begin(), rel.end()) - 1;
                    if (max_depth >= 0 && depth > max_depth) continue;
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
            result["files"] = files;

            if (truncated) result["truncated"] = 1;

            return result.dump(-1, ' ', false);
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
            std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return std::tolower(c); });

            if (needle.empty()) return R"({"e":"no_pattern"})";

            json out = json::array();

            int remaining = std::max(1, max_results);

            auto process = [&](const fs::directory_entry& entry) {
                if (remaining == 0) return false;

                std::string name = entry.path().filename().generic_string();
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

                if (lower.find(needle) == std::string::npos) return true;

                std::string rel = fs::relative(entry.path(), root).generic_string();

                if (entry.is_directory()) rel += "/";

                out.push_back(rel);

                --remaining;
                return remaining != 0;
            };

            if (recursive) {
                for (const auto& e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                    if (!process(e)) break;
                }
            } else {
                for (const auto& e : fs::directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                    if (!process(e)) break;
                }
            }

            return out.dump(-1, ' ', false);
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
                return result.dump(-1, ' ', false);
            }

            if (fs::is_regular_file(p)) result["s"] = fs::file_size(p);
            result["t"] = fs::is_directory(p) ? "d" : "f";

            return result.dump(-1, ' ', false);
        } catch (...) {
            return R"({"e":"ex"})";
        }
    }

    std::string read_file(const std::string& filepath_str, size_t chunk_size, size_t offset) {
        try {
            fs::path p(filepath_str);

            if (!fs::exists(p)) return R"({"e":"no_file"})";
            if (!fs::is_regular_file(p)) return R"({"e":"not_file"})";

            std::ifstream in(p, std::ios::binary);
            if (!in.is_open()) return R"({"e":"open_fail"})";

            size_t file_size = fs::file_size(p);

            if (offset >= file_size) {
                json out;
                out["c"] = "";
                out["o"] = offset;
                out["has_more"] = false;
                return out.dump();
            }

            in.seekg(offset);

            std::string content;
            content.resize(chunk_size);

            in.read(content.data(), chunk_size);

            size_t read_bytes = in.gcount();
            content.resize(read_bytes);

            size_t next_offset = offset + read_bytes;

            json out;
            out["c"] = content;
            if(next_offset < file_size) out["n"] = next_offset;

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
