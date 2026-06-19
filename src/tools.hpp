#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace tools {

struct FileChange {
    int id;
    std::string filepath;
    std::string timestamp;
    std::string original_content; // Snapshot of the whole file before the change
    std::string modified_content; // Snapshot of the whole file after the change
    std::string target_content;   // What was replaced
    std::string replacement_content; // What it was replaced with
    bool reverted = false;
};

// Tool functions exposed to the AI model
std::string list_directory(const std::string& path_str, bool recursive = true);
std::string read_file(const std::string& filepath_str);
std::string modify_file(const std::string& filepath_str, 
                        const std::string& target_content, 
                        const std::string& replacement_content, 
                        std::string& err_out);

// History management for GUI
bool revert_change(int change_id, std::string& err_out);
std::vector<FileChange> get_change_history();
void clear_change_history();

} // namespace tools
