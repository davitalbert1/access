#pragma once
#include <string>
#include <vector>

namespace semantic {
    // Configure the embedding server endpoints
    void set_server(const std::string& host, int port);
    void set_model(const std::string& model_name);
    std::string get_model();

    void index_project(const std::string& root);
    std::string retrieve_context(const std::string& query);
}