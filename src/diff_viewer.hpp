#pragma once

#include <string>
#include <vector>

namespace diff {

enum class LineType {
    Unchanged,
    Addition,
    Deletion
};

struct DiffLine {
    LineType type;
    int original_line_num; // 1-indexed, 0 if addition
    int modified_line_num; // 1-indexed, 0 if deletion
    std::string content;
};

// Generate line-by-line diff between two strings
std::vector<DiffLine> generate_diff(const std::string& original, const std::string& modified);

} // namespace diff
