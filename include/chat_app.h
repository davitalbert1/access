#pragma once
#include "../include/lm_client.h"
#include <string>
#include <vector>

namespace gui {

class ChatApp {
public:
    ChatApp();
    ~ChatApp();

    void init();
    void render();
    void cleanup();

private:
    lm::LMClient client;
    
    // Server configurations
    char host_buf[128];
    int port;
    char model_buf[128];
    
    // Chat UI state
    char input_buf[1024 * 16]; // Large buffer for multiline chat inputs
    char system_prompt_buf[1024 * 4]; // Buffer for editing system prompt
    bool is_generating = false;
    std::string current_status = "";
    bool scroll_to_bottom = false;
    
    // Connection checking state
    bool connected = false;
    std::string connection_error = "";
    float last_conn_check_time = 0.0f;
    
    // Tool/History state
    int selected_change_id = -1;
    bool show_diff_popup = false;
    std::string revert_error = "";
    
    // Revert message popup state
    bool show_revert_confirm_popup = false;
    std::string revert_confirm_msg_id = "";
    std::vector<std::string> revert_affected_files;
    
    // File Browser state
    char browser_path_buf[512];
    char file_filter_buf[128]; // Buffer for file browser filtering
    std::string selected_file_path = "";
    std::string selected_file_content = "";
    bool show_file_content_popup = false;
    
    // UI Panel Renderers
    void render_left_panel();
    void render_right_panel();
    void render_diff_popup();
    void render_file_content_popup();
    void render_revert_confirm_popup();
    
    // Helper to ping the LM Studio server
    void check_server_connection();
    
    // Apply styling resembling modern dark chat IDE
    void apply_custom_theme();
    
    // Persist and restore server settings
    void save_config();
    void load_config();
    static constexpr const char* CONFIG_FILE = "config.json";
};
}
