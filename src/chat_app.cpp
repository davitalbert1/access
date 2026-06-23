#include "chat_app.h"
#include "tools.h"
#include "diff_viewer.h"
#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
using json = nlohmann::json;

#ifdef _WIN32
#include <windows.h>
static std::string get_executable_directory() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string path_str(path);
    size_t pos = path_str.find_last_of("\\/");
    if (pos != std::string::npos) {
        return path_str.substr(0, pos);
    }
    return fs::current_path().generic_string();
}
#else
static std::string get_executable_directory() {
    return fs::current_path().generic_string();
}
#endif

static std::string wrap_text(const std::string& text, float wrap_width) {
    std::string result;
    std::string word;
    std::string current_line;
    
    auto flush_word = [&]() {
        if (word.empty()) return;
        std::string test_line = current_line.empty() ? word : current_line + " " + word;
        float width = ImGui::CalcTextSize(test_line.c_str()).x;
        if (width > wrap_width && !current_line.empty()) {
            result += current_line + "\n";
            current_line = word;
        } else {
            current_line = test_line;
        }
        word.clear();
    };

    for (char c : text) {
        if (c == '\n') {
            flush_word();
            result += current_line + "\n";
            current_line.clear();
        } else if (c == ' ' || c == '\t') {
            flush_word();
        } else {
            word += c;
        }
    }
    flush_word();
    if (!current_line.empty()) {
        result += current_line;
    }
    return result;
}

static std::string format_size(size_t size) {
    double size_d = static_cast<double>(size);
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    while (size_d >= 1024.0 && unit_idx < 4) {
        size_d /= 1024.0;
        unit_idx++;
    }
    if (unit_idx == 0) {
        return std::to_string(size) + " B";
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << size_d << " " << units[unit_idx];
    return ss.str();
}

namespace gui {

ChatApp::ChatApp() {
    std::strcpy(host_buf, "localhost");
    port = 1234;
    std::strcpy(model_buf, "default");
    std::strcpy(input_buf, "");
    std::strcpy(system_prompt_buf, "");
    std::strcpy(file_filter_buf, "");
    
    // Default workspace path is current path
    std::string current_path = fs::current_path().generic_string();
    std::strcpy(browser_path_buf, current_path.c_str());
    
    // Restore saved settings (host, port, model)
    load_config();
}

ChatApp::~ChatApp() {
}

void ChatApp::init() {
    apply_custom_theme();
    
    // Add startup info message detailing functions and types
    lm::Message startup_msg;
    startup_msg.role = "system_info";
    startup_msg.timestamp = "";
    startup_msg.content = 
        "=================== CAPACIDADES DO AGENTE C++ ===================\n"
        "O modelo de IA conectado possui acesso a estas funcoes locais:\n\n"
        "1. list_directory\n"
        "   - Descricao: Lista recursiva ou nao de arquivos e pastas no caminho.\n"
        "   - Parametros:\n"
        "     * path (string, obrigatorio): Caminho absoluto ou relativo.\n"
        "     * recursive (boolean, opcional, padrao=true): Listar recursivo.\n\n"
        "2. read_file\n"
        "   - Descricao: Le o conteudo completo do arquivo especificado.\n"
        "   - Parametros:\n"
        "     * path (string, obrigatorio): Caminho do arquivo.\n\n"
        "3. modify_file\n"
        "   - Descricao: Modifica trecho especifico por substitucao exata de bloco.\n"
        "     Se o arquivo nao existir e 'target_content' for vazio, cria um novo arquivo.\n"
        "   - Parametros:\n"
        "     * path (string, obrigatorio): Caminho do arquivo.\n"
        "     * target_content (string, obrigatorio): Texto exato a substituir.\n"
        "     * replacement_content (string, obrigatorio): Novo texto substituto.\n\n"
        "===============================================================\n"
        "Certifique-se de iniciar o 'Local Server' do LM Studio antes de enviar mensagens.\n"
        "Para testar, envie no chat: 'Verifique quais arquivos existem nesta pasta'";
        
    client.add_message(startup_msg);
    check_server_connection();
}

void ChatApp::check_server_connection() {
    // Strip http:// or https:// prefix if user pasted a full URL
    std::string h(host_buf);
    if (h.rfind("http://", 0) == 0)  h = h.substr(7);
    if (h.rfind("https://", 0) == 0) h = h.substr(8);
    // Remove trailing slash
    while (!h.empty() && h.back() == '/') h.pop_back();
    std::strcpy(host_buf, h.c_str());
    
    client.set_server(host_buf, port);
    client.set_model(model_buf);
    
    std::string err;
    connected = client.check_connection(err);
    if (!connected) {
        connection_error = err;
    } else {
        connection_error = "";
        std::strcpy(model_buf, client.get_model().c_str());
    }
    
    // Persist whatever the user configured
    save_config();
}

void ChatApp::apply_custom_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Enable rounded corners
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;
    
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    
    // Slates/Dark Palette
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.11f, 0.11f, 0.13f, 0.98f);
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    colors[ImGuiCol_FrameBg]                = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.18f, 0.18f, 0.21f, 1.00f);
    
    colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.08f, 0.08f, 0.10f, 0.50f);
    
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]    = ImVec4(0.32f, 0.32f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]     = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    
    colors[ImGuiCol_CheckMark]              = ImVec4(0.50f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.40f, 0.45f, 0.75f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.50f, 0.55f, 0.85f, 1.00f);
    
    colors[ImGuiCol_Button]                 = ImVec4(0.22f, 0.25f, 0.38f, 1.00f); // Slate Blue
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.28f, 0.32f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.18f, 0.20f, 0.30f, 1.00f);
    
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.25f, 0.28f, 0.40f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.18f, 0.20f, 0.30f, 1.00f);
    
    colors[ImGuiCol_Separator]              = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    
    colors[ImGuiCol_Tab]                    = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
}

void ChatApp::render() {
    // Fill the viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    
    ImGuiWindowFlags window_flags = 
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | 
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        
    if (ImGui::Begin("MainWorkspace", nullptr, window_flags)) {
        float width = ImGui::GetContentRegionAvail().x;
        
        // Split 50% / 50%
        ImGui::BeginChild("LeftPanel", ImVec2(width * 0.5f - 4.0f, 0), true);
        render_left_panel();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);
        render_right_panel();
        ImGui::EndChild();
        
        render_diff_popup();
        render_file_content_popup();
        render_revert_confirm_popup();
        
        ImGui::End();
    }
    
    // Save browser path if it has changed
    static std::string last_saved_browser_path = "";
    if (last_saved_browser_path != std::string(browser_path_buf)) {
        save_config();
        last_saved_browser_path = browser_path_buf;
    }
}

void ChatApp::render_left_panel() {
    // Chat Header / Connection Configs
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "CONEXAO DO SERVIDOR (LM STUDIO)");
    ImGui::Separator();
    
    ImGui::PushItemWidth(120.0f);
    ImGui::InputText("Host", host_buf, sizeof(host_buf));
    ImGui::SameLine();
    ImGui::InputInt("Porta", &port);
    ImGui::SameLine();
    ImGui::InputText("Modelo", model_buf, sizeof(model_buf));
    ImGui::PopItemWidth();
    
    ImGui::SameLine();
    if (ImGui::Button("Conectar / Atualizar")) {
        check_server_connection();
    }
    
    // Status text
    if (connected) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Conectado. Modelo ativo: %s", model_buf);
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Desconectado: %s", connection_error.c_str());
    }
    
    ImGui::Separator();
    
    // Chat messages history area
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "CONVERSA");
    float available_height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3.5f;
    
    ImGui::BeginChild("ChatLogs", ImVec2(0, available_height), true);
    auto history = client.get_history();
    for (size_t i = 0; i < history.size(); ++i) {
        auto& msg = history[i];
        
        if (msg.role == "system_info") {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.60f, 1.0f));
            
            // Copyable text
            std::string wrapped = wrap_text(msg.content, ImGui::GetContentRegionAvail().x - 15.0f);
            ImVec2 size = ImGui::CalcTextSize(wrapped.c_str());
            size.y += ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;
            size.x = ImGui::GetContentRegionAvail().x;
            
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            
            std::string id = "##msg_content_info_" + std::to_string(i);
            ImGui::InputTextMultiline(id.c_str(), const_cast<char*>(wrapped.c_str()), wrapped.size(), size, ImGuiInputTextFlags_ReadOnly);
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            
            ImGui::PopStyleColor();
            ImGui::Separator();
        } 
        else if (msg.role == "system") {
            // Internal System prompt usually hidden or shown small
            std::string header_id = "Prompt de Sistema (Oculto ao LLM)##" + std::to_string(i);
            if (ImGui::CollapsingHeader(header_id.c_str())) {
                // Copyable text
                std::string wrapped = wrap_text(msg.content, ImGui::GetContentRegionAvail().x - 15.0f);
                ImVec2 size = ImGui::CalcTextSize(wrapped.c_str());
                size.y += ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;
                size.x = ImGui::GetContentRegionAvail().x;
                
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                
                std::string id = "##msg_content_sys_" + std::to_string(i);
                ImGui::InputTextMultiline(id.c_str(), const_cast<char*>(wrapped.c_str()), wrapped.size(), size, ImGuiInputTextFlags_ReadOnly);
                
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
            }
        } 
        else if (msg.role == "tool") {
            std::string label = "Ferramenta executada: " + msg.name + " (" + msg.timestamp + ")##" + std::to_string(i);
            if (ImGui::CollapsingHeader(label.c_str())) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                
                // Copyable text
                std::string wrapped = wrap_text(msg.content, ImGui::GetContentRegionAvail().x - 15.0f);
                ImVec2 size = ImGui::CalcTextSize(wrapped.c_str());
                size.y += ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;
                size.x = ImGui::GetContentRegionAvail().x;
                
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                
                std::string id = "##msg_content_tool_" + std::to_string(i);
                ImGui::InputTextMultiline(id.c_str(), const_cast<char*>(wrapped.c_str()), wrapped.size(), size, ImGuiInputTextFlags_ReadOnly);
                
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
                
                ImGui::PopStyleColor();
            }
        } 
        else {
            bool is_user = (msg.role == "user");
            ImVec4 label_color = is_user ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f) : ImVec4(0.8f, 0.5f, 1.0f, 1.0f);
            std::string sender = is_user ? "Usuario" : "Modelo AI";
            
            ImGui::TextColored(label_color, "[%s] %s:", msg.timestamp.c_str(), sender.c_str());
            if (msg.role == "assistant" && !msg.msg_id.empty()) {
                auto changes = tools::get_change_history();
                bool has_changes = false;
                for (const auto& change : changes) {
                    if (change.message_id == msg.msg_id && !change.reverted) {
                        has_changes = true;
                        break;
                    }
                }
                if (has_changes) {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.15f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.1f, 0.1f, 1.0f));
                    std::string btn_label = "Reverter Alteracoes##" + msg.msg_id;
                    if (ImGui::SmallButton(btn_label.c_str())) {
                        revert_confirm_msg_id = msg.msg_id;
                        revert_affected_files.clear();
                        for (const auto& change : changes) {
                            if (change.message_id == msg.msg_id && !change.reverted) {
                                if (std::find(revert_affected_files.begin(), revert_affected_files.end(), change.filepath) == revert_affected_files.end()) {
                                    revert_affected_files.push_back(change.filepath);
                                }
                            }
                        }
                        if (!revert_affected_files.empty()) {
                            show_revert_confirm_popup = true;
                        }
                    }
                    ImGui::PopStyleColor(3);
                }
            }

            // Copyable text
            std::string wrapped = wrap_text(msg.content, ImGui::GetContentRegionAvail().x - 15.0f);
            ImVec2 size = ImGui::CalcTextSize(wrapped.c_str());
            size.y += ImGui::GetStyle().FramePadding.y * 2.0f + 4.0f;
            size.x = ImGui::GetContentRegionAvail().x;
            
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            
            std::string id = "##msg_content_" + std::to_string(i);
            ImGui::InputTextMultiline(id.c_str(), const_cast<char*>(wrapped.c_str()), wrapped.size(), size, ImGuiInputTextFlags_ReadOnly);
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            
            ImGui::Separator();
        }
    }
    
    if (scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
        scroll_to_bottom = false;
    }
    ImGui::EndChild();
    
    // Bottom generation status
    if (is_generating) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "IA: %s", current_status.c_str());
    } else {
        ImGui::Text(""); // Spacing
    }
    
    // Message Input bar
    ImGui::PushItemWidth(-160.0f);
    bool enter_pressed = ImGui::InputText("##ChatInput", input_buf, sizeof(input_buf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    
    bool send_clicked = ImGui::Button("Enviar", ImVec2(70, 0));
    ImGui::SameLine();
    
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.1f, 0.1f, 1.0f));
    bool clear_clicked = ImGui::Button("Limpar Chat", ImVec2(80, 0));
    ImGui::PopStyleColor(3);
    
    if (clear_clicked) {
        client.clear_history();
        lm::Message clear_msg;
        clear_msg.role = "system_info";
        clear_msg.timestamp = "";
        clear_msg.content = "Historico de conversa limpo.";
        client.add_message(clear_msg);
    }
    
    if ((enter_pressed || send_clicked) && !is_generating && std::strlen(input_buf) > 0) {
        std::string prompt(input_buf);
        std::strcpy(input_buf, "");
        
        is_generating = true;
        current_status = "Iniciando requisicao...";
        scroll_to_bottom = true;
        
        // Update custom system prompt in client
        client.set_custom_system_prompt(system_prompt_buf);
        
        // Dynamically set system prompt with current directory path appended
        std::string browser_path(browser_path_buf);
        std::string system_prompt = client.get_custom_system_prompt() + 
            "\nO diretorio de trabalho atual (aberto no visualizador de arquivos) e: " + browser_path + ". "
            "Use este diretorio como base para as operacoes de arquivos e pastas.";
        client.set_system_prompt(system_prompt);
        
        client.send_message(prompt, 
            [this](const std::string& status) {
                current_status = status;
            },
            [this](bool success, const std::string& final_text) {
                is_generating = false;
                scroll_to_bottom = true;
                if (!success) {
                    lm::Message err_msg;
                    err_msg.role = "system_info";
                    err_msg.timestamp = "";
                    err_msg.content = "ERRO NA EXECUCAO: " + final_text;
                    client.add_message(err_msg);
                }
            }
        );
    }
}

void ChatApp::render_right_panel() {
    if (ImGui::BeginTabBar("RightPanelTabs")) {
        // Tab 1: File Browser
        if (ImGui::BeginTabItem("Explorador de Arquivos")) {
            ImGui::InputText("Caminho Root", browser_path_buf, sizeof(browser_path_buf));
            
            // File search filter input
            ImGui::InputText("Filtrar arquivos", file_filter_buf, sizeof(file_filter_buf));
            
            ImGui::Separator();
            
            std::string list_json = tools::list_directory(browser_path_buf, false);
            try {
                json res = json::parse(list_json);
                if (res.contains("error")) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", res["error"].get<std::string>().c_str());
                } else {
                    auto& files = res["files"];
                    
                    // Directory Up button
                    if (ImGui::Button(".. [Voltar Pasta]")) {
                        fs::path current(browser_path_buf);
                        if (current.has_parent_path()) {
                            std::strcpy(browser_path_buf, current.parent_path().generic_string().c_str());
                        }
                    }
                    
                    ImGui::BeginChild("FileBrowserList", ImVec2(0, 0), true);
                    for (const auto& file : files) {
                        std::string name = file["name"].get<std::string>();
                        std::string path = file["path"].get<std::string>();
                        bool is_dir = file["is_directory"].get<bool>();
                        size_t size = file["size"].get<size_t>();
                        
                        // Check if file name matches the filter (case-insensitive)
                        if (std::strlen(file_filter_buf) > 0) {
                            std::string filter_str(file_filter_buf);
                            std::string name_lower = name;
                            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
                            std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);
                            if (name_lower.find(filter_str) == std::string::npos) {
                                continue;
                            }
                        }
                        
                        std::string icon = is_dir ? "[DIR] " : "[FILE] ";
                        std::string label = icon + name;
                        
                        if (is_dir) {
                            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                                if (ImGui::IsMouseDoubleClicked(0)) {
                                    std::strcpy(browser_path_buf, path.c_str());
                                }
                            }
                        } else {
                            if (ImGui::Selectable(label.c_str())) {
                                selected_file_path = path;
                                selected_file_content = tools::read_file(path);
                                show_file_content_popup = true;
                            }
                            ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                            ImGui::TextDisabled("%s", format_size(size).c_str());
                        }
                    }
                    ImGui::EndChild();
                }
            } catch (const std::exception& e) {
                ImGui::Text("Falha ao analisar diretorio: %s", e.what());
            }
            ImGui::EndTabItem();
        }
        
        // Tab 2: Change History
        if (ImGui::BeginTabItem("Historico de Alteracoes")) {
            auto history = tools::get_change_history();
            if (history.empty()) {
                ImGui::Text("Nenhuma alteracao efetuada pelo modelo ate o momento.");
            } else {
                if (ImGui::Button("Limpar Historico")) {
                    tools::clear_change_history();
                }
                ImGui::Separator();
                
                ImGui::BeginChild("HistoryList", ImVec2(0, 0), true);
                
                // Show in reverse order (newest first)
                for (int i = (int)history.size() - 1; i >= 0; --i) {
                    auto& change = history[i];
                    
                    ImGui::PushID(change.id);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "[ID: %d] %s", change.id, change.timestamp.c_str());
                    ImGui::Text("Arquivo: %s", change.filepath.c_str());
                    
                    if (change.original_content.empty()) {
                        ImGui::Text("Operacao: Criacao de Arquivo");
                    } else {
                        ImGui::Text("Operacao: Modificacao de Arquivo");
                    }
                    
                    if (change.reverted) {
                        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Status: REVERTIDO");
                    } else {
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Status: ATIVO");
                    }
                    
                    if (ImGui::Button("Ver Diff")) {
                        selected_change_id = change.id;
                        show_diff_popup = true;
                    }
                    
                    ImGui::SameLine();
                    
                    ImGui::BeginDisabled(change.reverted);
                    if (ImGui::Button("Reverter Alteracao")) {
                        std::string err;
                        if (!tools::revert_change(change.id, err)) {
                            revert_error = err;
                        } else {
                            revert_error = "";
                        }
                    }
                    ImGui::EndDisabled();
                    
                    if (selected_change_id == change.id && !revert_error.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Erro ao reverter: %s", revert_error.c_str());
                    }
                    
                    ImGui::Separator();
                    ImGui::PopID();
                }
                
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        
        // Tab 3: Tool Specs
        if (ImGui::BeginTabItem("Especificacoes de Ferramentas")) {
            ImGui::BeginChild("ToolSpecsList", ImVec2(0, 0), true);
            
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.6f, 1.0f), "FUNCOES C++ EXPOSTAS AO MODELO");
            ImGui::Separator();
            
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "1. list_directory");
            ImGui::Text("Descricao: Lista conteudo de um diretorio.");
            ImGui::Text("Argumentos:");
            ImGui::BulletText("path (string): Caminho do diretorio (Ex: 'd:/access')");
            ImGui::BulletText("recursive (boolean, opcional): Listar pastas recursivamente");
            ImGui::Separator();
            
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "2. read_file");
            ImGui::Text("Descricao: Le o conteudo de um arquivo em texto puro.");
            ImGui::Text("Argumentos:");
            ImGui::BulletText("path (string): Caminho absoluto ou relativo do arquivo");
            ImGui::Separator();
            
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "3. modify_file");
            ImGui::Text("Descricao: Substitui trecho de arquivo. Se o arquivo nao existe, cria-o.");
            ImGui::Text("Argumentos:");
            ImGui::BulletText("path (string): Caminho do arquivo");
            ImGui::BulletText("target_content (string): Bloco exato a ser substituido (deve ser unico). Vazio se for criar novo arquivo.");
            ImGui::BulletText("replacement_content (string): Bloco de texto novo");
            
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        
        // Tab 4: Advanced Settings & Stats
        if (ImGui::BeginTabItem("Configuracoes")) {
            ImGui::BeginChild("SettingsArea", ImVec2(0, 0), true);
            
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "PARAMETROS DO MODELO");
            ImGui::Separator();
            
            float temp = client.get_temperature();
            if (ImGui::SliderFloat("Temperatura", &temp, 0.0f, 2.0f, "%.2f")) {
                client.set_temperature(temp);
            }
            ImGui::TextDisabled("Valores baixos (ex: 0.2) sao mais precisos; altos sao mais criativos.");
            
            int max_t = client.get_max_tokens();
            bool limit_tokens = (max_t > 0);
            if (ImGui::Checkbox("Limitar tamanho da resposta (max_tokens)", &limit_tokens)) {
                if (limit_tokens) {
                    client.set_max_tokens(1024);
                } else {
                    client.set_max_tokens(-1);
                }
            }
            if (limit_tokens) {
                max_t = client.get_max_tokens();
                if (ImGui::InputInt("Tokens maximos", &max_t)) {
                    if (max_t < 1) max_t = 1;
                    client.set_max_tokens(max_t);
                }
            }
            
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "OTIMIZACAO DE CONTEXTO (TOKENS)");
            ImGui::Separator();
            
            int thresh = (int)client.get_pruning_threshold();
            if (ImGui::SliderInt("Limiar de Poda (Pruning)", &thresh, 5000, 100000, "%d chars")) {
                client.set_pruning_threshold((size_t)thresh);
            }
            ImGui::TextDisabled("Poda arquivos e diretorios antigos quando o historico enviado ultrapassar este limite.");
            
            // Statistics
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "ESTATISTICAS DE CONTEXTO");
            ImGui::Separator();
            
            size_t total_c = client.get_last_request_total_chars();
            size_t sent_c = client.get_last_request_sent_chars();
            
            ImGui::Text("Tamanho total do historico local: %zu caracteres", total_c);
            ImGui::Text("Tamanho real enviado ao modelo:  %zu caracteres", sent_c);
            
            if (total_c > 0) {
                if (sent_c < total_c) {
                    size_t saved = total_c - sent_c;
                    double percent = ((double)saved / total_c) * 100.0;
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), 
                        "Economia de Contexto: %zu caracteres salvos (~%.1f%%)", saved, percent);
                } else {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Nenhum dado podado no ultimo request.");
                }
            } else {
                ImGui::TextDisabled("Envie uma mensagem para exibir estatisticas.");
            }
            
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "PROMPT DE SISTEMA CUSTOMIZADO");
            ImGui::Separator();
            ImGui::InputTextMultiline("##SystemPrompt", system_prompt_buf, sizeof(system_prompt_buf), ImVec2(-1.0f, 180.0f));
            if (ImGui::Button("Redefinir Padrao")) {
                std::strcpy(system_prompt_buf, "Voce e um assistente de programacao util. "
                                               "Voce pode ler e modificar arquivos locais usando as ferramentas fornecidas. "
                                               "Sempre use a ferramenta modify_file se precisar editar um arquivo. "
                                               "Quando for ler pastas ou arquivos, informe os caminhos corretos. "
                                               "Sempre responda em Portugues.");
                client.set_custom_system_prompt(system_prompt_buf);
            }
            
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
}

void ChatApp::render_diff_popup() {
    if (show_diff_popup) {
        ImGui::OpenPopup("Visualizador de Diff");
    }
    
    // Style popup size
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Visualizador de Diff", &show_diff_popup)) {
        auto history = tools::get_change_history();
        auto it = std::find_if(history.begin(), history.end(), [this](const tools::FileChange& c) {
            return c.id == selected_change_id;
        });
        
        if (it != history.end()) {
            ImGui::Text("Visualizando Alteracao ID: %d", it->id);
            ImGui::Text("Arquivo: %s", it->filepath.c_str());
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Legenda: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "(-) Linhas Removidas ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "(+) Linhas Adicionadas");
            ImGui::Separator();
            
            ImGui::BeginChild("DiffViewerArea", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
            
            auto diff_lines = diff::generate_diff(it->original_content, it->modified_content);
            
            // Draw using table for perfect columns
            if (ImGui::BeginTable("DiffTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("Orig", ImGuiTableColumnFlags_WidthFixed, 35.0f);
                ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthFixed, 35.0f);
                ImGui::TableSetupColumn("T", ImGuiTableColumnFlags_WidthFixed, 20.0f);
                ImGui::TableSetupColumn("Conteudo", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                
                for (const auto& line : diff_lines) {
                    ImGui::TableNextRow();
                    
                    // Color row background depending on type
                    ImVec4 text_color = ImVec4(0.9f, 0.9f, 0.95f, 1.00f);
                    if (line.type == diff::LineType::Addition) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.1f, 0.3f, 0.1f, 0.4f)));
                        text_color = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
                    } else if (line.type == diff::LineType::Deletion) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.3f, 0.1f, 0.1f, 0.4f)));
                        text_color = ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
                    }
                    
                    // Original Line Number
                    ImGui::TableSetColumnIndex(0);
                    if (line.original_line_num > 0) {
                        ImGui::Text("%d", line.original_line_num);
                    } else {
                        ImGui::TextDisabled("-");
                    }
                    
                    // Modified Line Number
                    ImGui::TableSetColumnIndex(1);
                    if (line.modified_line_num > 0) {
                        ImGui::Text("%d", line.modified_line_num);
                    } else {
                        ImGui::TextDisabled("-");
                    }
                    
                    // Sign type
                    ImGui::TableSetColumnIndex(2);
                    if (line.type == diff::LineType::Addition) {
                        ImGui::TextColored(text_color, "+");
                    } else if (line.type == diff::LineType::Deletion) {
                        ImGui::TextColored(text_color, "-");
                    } else {
                        ImGui::Text(" ");
                    }
                    
                    // Content
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextColored(text_color, "%s", line.content.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
        } else {
            ImGui::Text("Erro: Alteracao nao encontrada.");
        }
        
        if (ImGui::Button("Fechar")) {
            show_diff_popup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ChatApp::render_file_content_popup() {
    if (show_file_content_popup) {
        ImGui::OpenPopup("Visualizar Arquivo");
    }
    
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Visualizar Arquivo", &show_file_content_popup)) {
        ImGui::Text("Arquivo: %s", selected_file_path.c_str());
        ImGui::Separator();
        
        ImGui::BeginChild("FileContentArea", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
        ImGui::TextUnformatted(selected_file_content.c_str());
        ImGui::EndChild();
        
        if (ImGui::Button("Fechar")) {
            show_file_content_popup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ChatApp::save_config() {
    try {
        json cfg;
        cfg["host"] = std::string(host_buf);
        cfg["port"] = port;
        cfg["model"] = std::string(model_buf);
        cfg["browser_path"] = std::string(browser_path_buf);
        
        // Advanced configurations
        cfg["temperature"] = client.get_temperature();
        cfg["max_tokens"] = client.get_max_tokens();
        cfg["pruning_threshold"] = client.get_pruning_threshold();
        cfg["custom_system_prompt"] = std::string(system_prompt_buf);
        
        std::ofstream out(CONFIG_FILE);
        if (out.is_open()) {
            out << cfg.dump(2);
        }
    } catch (const std::exception& e) {}
}

void ChatApp::load_config() {
    // Set default buffers in case config parsing fails
    std::strcpy(file_filter_buf, "");
    std::strcpy(system_prompt_buf, client.get_custom_system_prompt().c_str());
    
    try {
        std::ifstream in(CONFIG_FILE);
        if (in.is_open()) {
            json cfg = json::parse(in);
            
            if (cfg.contains("host") && cfg["host"].is_string()) {
                std::string h = cfg["host"].get<std::string>();
                std::strcpy(host_buf, h.c_str());
            }
            if (cfg.contains("port") && cfg["port"].is_number_integer()) {
                port = cfg["port"].get<int>();
            }
            if (cfg.contains("model") && cfg["model"].is_string()) {
                std::string m = cfg["model"].get<std::string>();
                std::strcpy(model_buf, m.c_str());
            }
            
            // Advanced parameters
            if (cfg.contains("temperature") && cfg["temperature"].is_number()) {
                client.set_temperature(cfg["temperature"].get<float>());
            }
            if (cfg.contains("max_tokens") && cfg["max_tokens"].is_number_integer()) {
                client.set_max_tokens(cfg["max_tokens"].get<int>());
            }
            if (cfg.contains("pruning_threshold") && cfg["pruning_threshold"].is_number_integer()) {
                client.set_pruning_threshold(cfg["pruning_threshold"].get<size_t>());
            }
            if (cfg.contains("custom_system_prompt") && cfg["custom_system_prompt"].is_string()) {
                std::string csp = cfg["custom_system_prompt"].get<std::string>();
                std::strcpy(system_prompt_buf, csp.c_str());
                client.set_custom_system_prompt(csp);
            }
            
            if (cfg.contains("browser_path") && cfg["browser_path"].is_string()) {
                std::string bp = cfg["browser_path"].get<std::string>();
                fs::path p(bp);
                if (fs::exists(p) && fs::is_directory(p)) {
                    std::strcpy(browser_path_buf, bp.c_str());
                } else {
                    std::strcpy(browser_path_buf, get_executable_directory().c_str());
                }
            } else {
                std::strcpy(browser_path_buf, get_executable_directory().c_str());
            }
        } else {
            std::strcpy(browser_path_buf, get_executable_directory().c_str());
        }
    } catch (const std::exception& e) {
        std::strcpy(browser_path_buf, get_executable_directory().c_str());
    }
}

void ChatApp::render_revert_confirm_popup() {
    if (show_revert_confirm_popup) {
        ImGui::OpenPopup("Confirmar Reversao");
    }
    
    ImGui::SetNextWindowSize(ImVec2(550, 260), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Confirmar Reversao", &show_revert_confirm_popup, ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "AVISO: Esta acao ira desfazer todas as alteracoes desta mensagem!");
        ImGui::TextWrapped("Deseja restaurar o conteudo original dos seguintes arquivos?");
        ImGui::Separator();
        
        ImGui::BeginChild("RevertFilesList", ImVec2(0, 110), true);
        for (const auto& file : revert_affected_files) {
            ImGui::BulletText("%s", file.c_str());
        }
        ImGui::EndChild();
        
        ImGui::Separator();
        
        if (ImGui::Button("Sim, Reverter", ImVec2(120, 0))) {
            std::string err;
            auto changes = tools::get_change_history();
            bool success = true;
            // Revert in reverse order (newest first)
            for (int i = (int)changes.size() - 1; i >= 0; --i) {
                auto& change = changes[i];
                if (change.message_id == revert_confirm_msg_id && !change.reverted) {
                    if (!tools::revert_change(change.id, err)) {
                        success = false;
                        std::cerr << "Erro ao reverter alteracao: " << err << std::endl;
                    }
                }
            }
            show_revert_confirm_popup = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) {
            show_revert_confirm_popup = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

} // namespace gui
