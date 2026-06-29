#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

namespace lm {
    struct Message {
        std::string role; // "user", "assistant", "system", "tool"
        std::string content;
        std::string name; // For tool results (function name)
        std::string tool_call_id; // For tool results and matching tool calls
        nlohmann::json tool_calls; // Parsed tool calls from the assistant if any
        std::string timestamp;
        std::string reasoning;
        std::string msg_id; // Unique message identifier
    };

    class LMClient {
    public:
        LMClient();
        ~LMClient();

        // Configuration
        void set_server(const std::string& host, int port);
        void set_model(const std::string& model_name);
        
        std::string get_host() const {
            return host;
        }

        int get_port() const {
            return port;
        }

        std::string get_model() const {
            return model;
        }

        // Advanced Model & Optimization Parameters
        void set_temperature(float temp) { temperature = temp; }
        float get_temperature() const { return temperature; }
        
        void set_max_tokens(int max_t) { max_tokens = max_t; }
        int get_max_tokens() const { return max_tokens; }
        
        void set_pruning_threshold(size_t threshold) { pruning_threshold = threshold; }
        size_t get_pruning_threshold() const { return pruning_threshold; }
        
        void set_custom_system_prompt(const std::string& prompt) { custom_system_prompt = prompt; }
        std::string get_custom_system_prompt() const { return custom_system_prompt; }

        // Request Stats
        size_t get_last_request_total_chars() const { return last_request_total_chars; }
        size_t get_last_request_sent_chars() const { return last_request_sent_chars; }

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

        // Advanced configs
        float temperature = 0.7f;
        int max_tokens = -1; // -1 means disabled
        size_t pruning_threshold = 25000;
        std::string custom_system_prompt;

        // Stats
        size_t last_request_total_chars = 0;
        size_t last_request_sent_chars = 0;

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
}
