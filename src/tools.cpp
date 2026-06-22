#include "tools.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
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
        if (!fs::exists(p)) {
            return json({{"error", "Path does not exist: " + path_str}}).dump();
        }
        if (!fs::is_directory(p)) {
            return json({{"error", "Path is not a directory: " + path_str}}).dump();
        }

        json files = json::array();
        int count = 0;
        const int MAX_FILES = 500; // Limit total items to prevent huge messages
        bool truncated = false;

        auto add_entry = [&](const fs::directory_entry& entry) {
            if (count >= MAX_FILES) {
                truncated = true;
                return;
            }
            json item;
            item["name"] = entry.path().filename().string();
            item["path"] = fs::absolute(entry.path()).generic_string();
            item["is_directory"] = entry.is_directory();
            if (entry.is_regular_file()) {
                item["size"] = entry.file_size();
            } else {
                item["size"] = 0;
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

        json result = json::object();
        result["files"] = files;
        result["count"] = count;
        result["truncated"] = truncated;
        if (truncated) {
            result["warning"] = "Directory list truncated to " + std::to_string(MAX_FILES) + " items.";
        }
        return result.dump(2);
    } catch (const std::exception& e) {
        return json({{"error", std::string("Failed to list directory: ") + e.what()}}).dump();
    }
}

std::string read_file(const std::string& filepath_str) {
    try {
        fs::path p(filepath_str);
        if (!fs::exists(p)) {
            return "Error: File does not exist: " + filepath_str;
        }
        if (!fs::is_regular_file(p)) {
            return "Error: Path is not a regular file: " + filepath_str;
        }
        std::ifstream in(p, std::ios::in | std::ios::binary);
        if (!in.is_open()) {
            return "Error: Could not open file for reading: " + filepath_str;
        }
        std::ostringstream sstr;
        sstr << in.rdbuf();
        return sstr.str();
    } catch (const std::exception& e) {
        return "Error reading file: " + std::string(e.what());
    }
}

std::string modify_file(const std::string& filepath_str, 
                        const std::string& target_content, 
                        const std::string& replacement_content, 
                        std::string& err_out) {
    try {
        fs::path p(filepath_str);
        std::string absolute_filepath = fs::absolute(p).generic_string();
        std::string original = "";
        bool is_new_file = !fs::exists(p);

        if (is_new_file) {
            if (!target_content.empty()) {
                err_out = "File does not exist. To create a new file, target_content must be empty.";
                return "Error: " + err_out;
            }
        } else {
            if (!fs::is_regular_file(p)) {
                err_out = "Path is not a regular file: " + filepath_str;
                return "Error: " + err_out;
            }
            // Read file content
            std::ifstream in(p, std::ios::in | std::ios::binary);
            if (!in.is_open()) {
                err_out = "Could not open file for reading: " + filepath_str;
                return "Error: " + err_out;
            }
            std::ostringstream sstr;
            sstr << in.rdbuf();
            original = sstr.str();
        }

        std::string modified = "";
        if (is_new_file) {
            modified = replacement_content;
        } else {
            // If target_content is empty, we don't know where to insert unless the file is empty
            if (target_content.empty()) {
                if (original.empty()) {
                    modified = replacement_content;
                } else {
                    err_out = "target_content is empty, but the file is not empty. Please specify what content to replace.";
                    return "Error: " + err_out;
                }
            } else {
                size_t first_pos = original.find(target_content);
                if (first_pos == std::string::npos) {
                    err_out = "Target content block not found in the file.";
                    return "Error: " + err_out;
                }
                size_t second_pos = original.find(target_content, first_pos + target_content.length());
                if (second_pos != std::string::npos) {
                    err_out = "Target content block is not unique. It occurs multiple times in the file.";
                    return "Error: " + err_out;
                }
                
                // Perform replacement
                modified = original;
                modified.replace(first_pos, target_content.length(), replacement_content);
            }
        }

        // Create parent directories if they don't exist
        fs::create_directories(p.parent_path());

        // Write modified content back to file
        std::ofstream out(p, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            err_out = "Could not open file for writing: " + filepath_str;
            return "Error: " + err_out;
        }
        out << modified;
        out.close();

        // Save change in history
        FileChange change;
        {
            std::lock_guard<std::mutex> lock(history_mutex);
            change.id = global_change_counter++;
            change.filepath = absolute_filepath;
            change.timestamp = get_current_timestamp();
            change.original_content = original;
            change.modified_content = modified;
            change.target_content = target_content;
            change.replacement_content = replacement_content;
            change.reverted = false;
            change.message_id = current_tool_message_id;
            history.push_back(change);
        }

        return "Success: File modified. Change ID: " + std::to_string(change.id);
    } catch (const std::exception& e) {
        err_out = std::string("Exception during file modify: ") + e.what();
        return "Error: " + err_out;
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
                    // It was a new file creation, delete it
                    if (fs::exists(p)) {
                        fs::remove(p);
                    }
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

} // namespace tools
