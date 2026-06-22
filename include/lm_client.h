#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

namespace lm {

struct Message {
    std::string role;          // "user", "assistant", "system", "tool"
    std::string content;
    std::string name;          // For tool results (function name)
    std::string tool_call_id;  // For tool results and matching tool calls
    nlohmann::json tool_calls; // Parsed tool calls from the assistant if any
    std::string timestamp;
    std::string msg_id;        // Unique message identifier
};

class LMClient {
public:
    LMClient();
    ~LMClient();

    // Configuration
    void set_server(const std::string& host, int port);
    void set_model(const std::string& model_name);
    
    std::string get_host() const { return host; }
    int get_port() const { return port; }
    std::string get_model() const { return model; }

    // Test connection to the local server
    bool check_connection(std::string& err_out);

    // Send a message and handle tool completion loop
    // Callbacks will be executed in a worker thread
    void send_message(const std::string& user_prompt,
                      std::function<void(const std::string& status)> on_status_change,
                      std::function<void(bool success, const std::string& final_text)> on_complete);

    // History management
    std::vector<Message> get_history();
    void add_message(const Message& msg);
    void clear_history();
    void set_system_prompt(const std::string& prompt);

private:
    std::string host = "localhost";
    int port = 1234;
    std::string model = "default"; // Will auto-detect or use user defined

    std::vector<Message> history;
    std::mutex history_mutex;

    // Internal HTTP request helper
    bool make_post_request(const std::string& url, const std::string& post_data, std::string& response_out, std::string& err_out);
    bool make_get_request(const std::string& url, std::string& response_out, std::string& err_out);
    
    // Internal loop to execute tool calls
    bool run_tool_calls_loop(const nlohmann::json& tool_calls, 
                             std::function<void(const std::string& status)> on_status_change, 
                             std::string& err_out);
};

} // namespace lm
