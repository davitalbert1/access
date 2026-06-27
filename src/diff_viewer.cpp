#include "../include/diff_viewer.h"
#include <sstream>
#include <algorithm>
#include <vector>

namespace diff {

static std::vector<std::string> split_lines(const std::string& str) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream stream(str);
    while (std::getline(stream, line)) {
        // Strip trailing \r (Windows endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    // Handle empty file/string or ending with a newline
    if (str.empty()) lines.push_back("");
    return lines;
}

std::vector<DiffLine> generate_diff(const std::string& original, const std::string& modified) {
    auto lines_orig = split_lines(original);
    auto lines_mod = split_lines(modified);
    
    int m = lines_orig.size();
    int n = lines_mod.size();
    
    // DP table for LCS
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            if (lines_orig[i - 1] == lines_mod[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }
    
    // Backtrack to build diff in reverse order
    std::vector<DiffLine> result;
    int i = m, j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && lines_orig[i - 1] == lines_mod[j - 1]) {
            result.push_back({ LineType::Unchanged, i, j, lines_orig[i - 1] });
            i--;
            j--;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            result.push_back({ LineType::Addition, 0, j, lines_mod[j - 1] });
            j--;
        } else if (i > 0 && (j == 0 || dp[i][j - 1] < dp[i - 1][j])) {
            result.push_back({ LineType::Deletion, i, 0, lines_orig[i - 1] });
            i--;
        }
    }
    
    std::reverse(result.begin(), result.end());
    return result;
}
}
