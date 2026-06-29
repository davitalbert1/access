#pragma once
#include <string>
#include <vector>

namespace semantic {

void index_project(const std::string& root);
std::string retrieve_context(const std::string& query);

}