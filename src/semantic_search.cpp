#include "../include/semantic_search.h"
#include "../include/tools.h"

#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <cstring>
#include <sstream>
#include <iostream>

using json = nlohmann::json;

namespace semantic {

// Embedding server configuration
static std::string embed_host = "127.0.0.1";
static int embed_port = 1234;
static std::string embed_model = "text-embedding-nomic-embed-text-v1.5";
static std::mutex embed_mutex;

struct Entry {
    std::string path;
    std::string text;
    std::vector<float> embedding;
};

static std::vector<Entry> index;

void set_server(const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(embed_mutex);
    embed_host = host;
    embed_port = port;
}

void set_model(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(embed_mutex);
    embed_model = model_name;
}

std::string get_model() {
    std::lock_guard<std::mutex> lock(embed_mutex);
    return embed_model;
}

// Write callback for curl
static size_t embed_curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* s = (std::string*)userp;
    s->append((char*)contents, realsize);
    return realsize;
}

// Request embedding from LM Studio API
static std::vector<float> request_embedding(const std::string& text) {
    std::lock_guard<std::mutex> lock(embed_mutex);

    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::string url = "http://" + embed_host + ":" + std::to_string(embed_port) + "/v1/embeddings";

    json req = json::object();
    req["model"] = embed_model;
    req["input"] = text;

    std::string post_data = req.dump();
    std::string response_data;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, embed_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[Embedding] curl error: " << curl_easy_strerror(res) << std::endl;
        return {};
    }

    try {
        json resp = json::parse(response_data);
        if (resp.contains("data") && resp["data"].is_array() && !resp["data"].empty()) {
            auto& data = resp["data"][0];
            if (data.contains("embedding") && data["embedding"].is_array()) {
                std::vector<float> embedding;
                for (const auto& val : data["embedding"]) {
                    embedding.push_back(val.get<float>());
                }
                return embedding;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Embedding] parse error: " << e.what() << std::endl;
    }

    return {};
}

static float cosine(const std::vector<float>& a,
                    const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0, na = 0, nb = 0;

    for (size_t i = 0; i < a.size(); i++) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }

    float denom = std::sqrt(na) * std::sqrt(nb);
    if (denom < 1e-10f) return 0.0f;

    return dot / denom;
}

void index_project(const std::string& root) {
    index.clear();

    auto list = json::parse(tools::list_directory(root, true, false));

    for (auto& f : list["files"]) {
        if (f.value("d", false)) continue;

        // Read file with limit (4096 chars per file to save tokens/processing)
        auto file = json::parse(tools::read_file(f["p"], 4096, 0));

        if (!file.contains("c")) continue;

        std::string file_text = file["c"];

        // Skip binary/empty files
        if (file_text.empty()) continue;

        Entry e;
        e.path = f["p"];

        // Only store first 2000 chars for indexing to save tokens
        e.text = file_text.substr(0, 2000);

        // Get real embedding from LM Studio API
        e.embedding = request_embedding(e.text);

        // If embedding failed, skip this entry
        if (e.embedding.empty()) {
            std::cerr << "[Embedding] Failed to embed: " << e.path << std::endl;
            continue;
        }

        index.push_back(e);
    }
}

std::string retrieve_context(const std::string& query) {
    if (index.empty()) return "";

    auto q = request_embedding(query);

    // If embedding query fails, fall back to empty context
    if (q.empty()) return "";

    std::vector<std::pair<float, Entry*>> scored;

    for (auto& e : index) {
        float s = cosine(q, e.embedding);
        scored.push_back({s, &e});
    }

    // Sort by score descending
    std::sort(scored.begin(), scored.end(),
        [](auto& a, auto& b) { return a.first > b.first; });

    std::string ctx;
    int chars_used = 0;
    const int MAX_CTX_CHARS = 4000; // Limit total context chars to save tokens

    for (int i = 0; i < 3 && i < (int)scored.size(); i++) {
        // Skip low-relevance results (below 0.5 cosine similarity)
        if (scored[i].first < 0.5f && i > 0) continue;

        std::string header = "FILE: " + scored[i].second->path + "\n";
        std::string body;
        int remaining = MAX_CTX_CHARS - chars_used - (int)header.size() - 2;
        if (remaining <= 0) break;

        body = scored[i].second->text.substr(0, (size_t)remaining);

        ctx += header;
        ctx += body;
        ctx += "\n\n";
        chars_used += (int)header.size() + (int)body.size() + 2;
    }

    return ctx;
}

} // namespace semantic