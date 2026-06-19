#include "chat_app.hpp"
#include "tools.hpp"
#include "diff_viewer.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace gui {

ChatApp::ChatApp() {
    std::strcpy(host_buf, "localhost");
    port = 1234;
    std::strcpy(model_buf, "default");
    std::strcpy(input_buf, "");
    
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
        
        ImGui::End();
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
            ImGui::TextWrapped("%s", msg.content.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        } 
        else if (msg.role == "system") {
            // Internal System prompt usually hidden or shown small
            if (ImGui::CollapsingHeader("Prompt de Sistema (Oculto ao LLM)")) {
                ImGui::TextWrapped("%s", msg.content.c_str());
            }
        } 
        else if (msg.role == "tool") {
            std::string label = "Ferramenta executada: " + msg.name + " (" + msg.timestamp + ")";
            if (ImGui::CollapsingHeader(label.c_str())) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                ImGui::TextWrapped("%s", msg.content.c_str());
                ImGui::PopStyleColor();
            }
        } 
        else {
            bool is_user = (msg.role == "user");
            ImVec4 label_color = is_user ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f) : ImVec4(0.8f, 0.5f, 1.0f, 1.0f);
            std::string sender = is_user ? "Usuario" : "Modelo AI";
            
            ImGui::TextColored(label_color, "[%s] %s:", msg.timestamp.c_str(), sender.c_str());
            ImGui::TextWrapped("%s", msg.content.c_str());
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
    ImGui::PushItemWidth(-70.0f);
    bool enter_pressed = ImGui::InputText("##ChatInput", input_buf, sizeof(input_buf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    
    bool send_clicked = ImGui::Button("Enviar", ImVec2(60, 0));
    
    if ((enter_pressed || send_clicked) && !is_generating && std::strlen(input_buf) > 0) {
        std::string prompt(input_buf);
        std::strcpy(input_buf, "");
        
        is_generating = true;
        current_status = "Iniciando requisicao...";
        scroll_to_bottom = true;
        
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
            ImGui::SameLine();
            if (ImGui::Button("Recarregar")) {
                // Do nothing, list will refresh on loop
            }
            
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
                            ImGui::TextDisabled("%zu B", size);
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
        
        std::ofstream out(CONFIG_FILE);
        if (out.is_open()) {
            out << cfg.dump(2);
            std::cout << "[Config] Configuracoes salvas em " << CONFIG_FILE << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Config] Erro ao salvar: " << e.what() << std::endl;
    }
}

void ChatApp::load_config() {
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
            
            std::cout << "[Config] Configuracoes carregadas de " << CONFIG_FILE << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Config] Erro ao carregar (usando padroes): " << e.what() << std::endl;
    }
}

} // namespace gui
