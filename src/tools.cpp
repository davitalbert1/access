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

    std::string list_directory(const std::string& path_str, bool recursive, bool show_excluded) {
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

                json item;
                fs::path absolute_entry = fs::absolute(entry.path());
                item["p"] = absolute_entry.generic_string();
                item["d"] = entry.is_directory();
                if (!entry.is_directory()) item["s"] = entry.file_size();

                try {
                    auto ftime = fs::last_write_time(entry.path());
                    auto sctime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                    );
                    std::time_t cftime = std::chrono::system_clock::to_time_t(sctime);
                    char timebuf[32];
                    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", std::localtime(&cftime));
                    item["m"] = timebuf;
                } catch (...) {
                    item["m"] = "";
                }

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
            result["files"] = files;

            if (truncated) result["truncated"] = true;

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
            std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (needle.empty()) return R"({"error":"pattern_required","matches":[],"count":0})";

            std::vector<fs::directory_entry> all_entries;

            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                    all_entries.push_back(entry);
                }
            } else {
                for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                    all_entries.push_back(entry);
                }
            }

            std::sort(all_entries.begin(), all_entries.end(), [&](const fs::directory_entry& a, const fs::directory_entry& b) {
                std::string name_a = a.path().filename().generic_string();
                std::string name_b = b.path().filename().generic_string();
                std::string lower_a = name_a, lower_b = name_b;
                std::transform(lower_a.begin(), lower_a.end(), lower_a.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                std::transform(lower_b.begin(), lower_b.end(), lower_b.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });

                bool a_starts = !needle.empty() && lower_a.rfind(needle, 0) == 0;
                bool b_starts = !needle.empty() && lower_b.rfind(needle, 0) == 0;
                if (a_starts != b_starts) return a_starts > b_starts;

                bool a_contains = !needle.empty() && lower_a.find(needle) != std::string::npos;
                bool b_contains = !needle.empty() && lower_b.find(needle) != std::string::npos;
                if (a_contains != b_contains) return a_contains > b_contains;

                bool a_dir = a.is_directory();
                bool b_dir = b.is_directory();
                if (a_dir != b_dir) return a_dir > b_dir;

                int depth_a = std::distance(a.path().begin(), a.path().end());
                int depth_b = std::distance(b.path().begin(), b.path().end());
                if (depth_a != depth_b) return depth_a < depth_b;

                return lower_a < lower_b;
            });

            json matches = json::array();
            int count = 0;
            const int MAX_RESULTS = std::max(1, max_results);

            for (const auto& entry : all_entries) {
                if (count >= MAX_RESULTS) break;

                std::string name = entry.path().filename().generic_string();
                std::string lower_name = name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });

                if (!needle.empty() && lower_name.find(needle) == std::string::npos) continue;

                json item;
                item["p"] = entry.path().generic_string();
                item["d"] = entry.is_directory();
                if (!entry.is_directory()) item["s"] = entry.file_size();

                matches.push_back(item);
                count++;
            }

            json result;
            result["matches"] = matches;
            result["count"] = count;
            return result.dump(-1, ' ', false);
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

            result["exists"] = true;
            result["n"] = p.filename().generic_string();
            result["p"] = fs::absolute(p).generic_string();
            result["parent"] = p.parent_path().generic_string();
            result["is_directory"] = fs::is_directory(p);
            result["is_file"] = fs::is_regular_file(p);
            if (fs::is_regular_file(p)) result["size"] = fs::file_size(p);

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
            out["o"] = offset;
            out["n"] = next_offset;
            out["has_more"] = next_offset < file_size;

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
