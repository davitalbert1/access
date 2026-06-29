#include "../include/semantic_search.h"
#include "../include/tools.h"

#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

namespace semantic {

struct Entry {
    std::string path;
    std::string text;
    std::vector<float> embedding;
};

static std::vector<Entry> index;

static std::vector<float> embed(const std::string& text) {
    std::vector<float> v(32, 0.0f);
    for (size_t i = 0; i < text.size(); ++i) v[i % v.size()] += static_cast<unsigned char>(text[i]);
    return v;
}

static float cosine(const std::vector<float>& a,
                    const std::vector<float>& b) {
    float dot = 0, na = 0, nb = 0;

    for (size_t i = 0; i < a.size(); i++) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }

    return dot / (sqrt(na) * sqrt(nb));
}

void index_project(const std::string& root) {
    index.clear();

    auto list = json::parse(tools::list_directory(root, true, false));

    for (auto& f : list["files"]) {
        if (f.value("d", false)) continue;

        auto file = json::parse(tools::read_file(f["p"], 24 * 1024, 0));

        if (!file.contains("c")) continue;

        Entry e;
        e.path = f["p"];
        e.text = file["c"];
        e.embedding = embed(e.text);

        index.push_back(e);
    }
}

std::string retrieve_context(const std::string& query) {
    auto q = embed(query);

    std::vector<std::pair<float, Entry*>> scored;

    for (auto& e : index) {
        float s = cosine(q, e.embedding);
        scored.push_back({s, &e});
    }

    std::sort(scored.begin(), scored.end(),
        [](auto& a, auto& b) { return a.first > b.first; });

    std::string ctx;

    for (int i = 0; i < 3 && i < (int)scored.size(); i++) {
        ctx += "FILE: " + scored[i].second->path + "\n";
        ctx += scored[i].second->text.substr(0, 2000);
        ctx += "\n\n";
    }

    return ctx;
}

}