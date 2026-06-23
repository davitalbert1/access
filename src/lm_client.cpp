#include "lm_client.h"
#include "tools.h"
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <atomic>

static std::atomic<int> global_msg_id_counter{1};

using json = nlohmann::json;

namespace lm {

static std::string format_list_directory_compact(const std::string& json_str) {
    try {
        json res = json::parse(json_str);
        if (res.contains("error")) {
            return "Erro ao listar diretorio: " + res["error"].get<std::string>();
        }
        if (!res.contains("files") || !res["files"].is_array()) {
            return json_str;
        }
        std::stringstream ss;
        auto& files = res["files"];
        ss << "Itens no diretorio (" << files.size() << " itens):\n";
        for (const auto& file : files) {
            std::string path = file.value("path", "");
            bool is_dir = file.value("is_directory", false);
            size_t size = file.value("size", 0);
            
            ss << "- " << (is_dir ? "[DIR]" : "[FILE]") << " " << path;
            if (!is_dir) {
                ss << " (" << size << " bytes)";
            }
            ss << "\n";
        }
        if (res.value("truncated", false)) {
            ss << "... (lista truncada para " << files.size() << " itens)\n";
        }
        return ss.str();
    } catch (...) {
        return json_str;
    }
}

// Helper to get current timestamp
static std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    return ss.str();
}

// Write callback for curl
static size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* s = (std::string*)userp;
    s->append((char*)contents, realsize);
    return realsize;
}

LMClient::LMClient() {
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
    }
    
    // Set a default system prompt
    custom_system_prompt = "Voce e um assistente de programacao util. "
                           "Voce pode ler e modificar arquivos locais usando as ferramentas fornecidas. "
                           "Sempre use a ferramenta modify_file se precisar editar um arquivo. "
                           "Quando for ler pastas ou arquivos, informe os caminhos corretos. "
                           "Sempre responda em Portugues.";
    set_system_prompt(custom_system_prompt);
}

LMClient::~LMClient() {
    // curl_global_cleanup(); // typically clean up at exit, but okay to let OS handle it
}

void LMClient::set_server(const std::string& h, int p) {
    host = h;
    port = p;
}

void LMClient::set_model(const std::string& model_name) {
    model = model_name;
}

bool LMClient::make_get_request(const std::string& url, std::string& response_out, std::string& err_out) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "Failed to initialize curl";
        return false;
    }

    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5s timeout for status checks

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        err_out = curl_easy_strerror(res);
        return false;
    }

    if (http_code != 200) {
        err_out = "HTTP status code " + std::to_string(http_code);
        return false;
    }

    response_out = response_data;
    return true;
}

bool LMClient::make_post_request(const std::string& url, const std::string& post_data, std::string& response_out, std::string& err_out) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "Failed to initialize curl";
        return false;
    }

    std::string response_data;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); // No timeout - wait indefinitely for LLM response

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        err_out = curl_easy_strerror(res);
        return false;
    }

    if (http_code != 200) {
        err_out = "HTTP status code " + std::to_string(http_code) + "\nResponse: " + response_data;
        return false;
    }

    response_out = response_data;
    return true;
}

bool LMClient::check_connection(std::string& err_out) {
    std::string url = "http://" + host + ":" + std::to_string(port) + "/v1/models";
    std::string response;
    if (make_get_request(url, response, err_out)) {
        try {
            json res = json::parse(response);
            if (res.contains("data") && res["data"].is_array() && !res["data"].empty()) {
                // Autodetect model from server if currently set to "default" or empty
                std::string first_model = res["data"][0]["id"].get<std::string>();
                if (model == "default" || model.empty()) {
                    model = first_model;
                }
            }
            return true;
        } catch (...) {
            // Parsed incorrectly, but HTTP request worked
            return true;
        }
    }
    return false;
}

void LMClient::send_message(const std::string& user_prompt,
                            std::function<void(const std::string& status)> on_status_change,
                            std::function<void(bool success, const std::string& final_text)> on_complete) {
    std::string turn_id = "msg_" + std::to_string(global_msg_id_counter++);
    std::thread([this, user_prompt, turn_id, on_status_change, on_complete]() {
        tools::current_tool_message_id = turn_id;
        // Add User message
        {
            std::lock_guard<std::mutex> lock(history_mutex);
            history.push_back({ "user", user_prompt, "", "", nullptr, get_current_timestamp(), turn_id });
        }
        
        on_status_change("Enviando prompt ao modelo...");
        
        bool loop = true;
        std::string final_content = "";
        std::string err_out = "";
        
        while (loop) {
            // Build request json
            json req = json::object();
            {
                std::lock_guard<std::mutex> lock(history_mutex);
                json messages = json::array();
                
                // Calculate total size of the message history to check if pruning is needed
                size_t total_chars = 0;
                size_t sent_chars = 0;
                for (const auto& msg : history) {
                    if (msg.role == "system_info") continue;
                    total_chars += msg.content.length();
                }
                
                bool need_pruning = (total_chars > pruning_threshold);
                
                for (const auto& msg : history) {
                    // Skip internal UI-only messages
                    if (msg.role == "system_info") continue;
                    
                    json m = { {"role", msg.role} };
                    
                    std::string content_to_send = msg.content;
                    
                    // 1. Always format list_directory responses compactly to save tokens
                    if (msg.role == "tool" && msg.name == "list_directory") {
                        content_to_send = format_list_directory_compact(msg.content);
                    }
                    
                    // 2. Prune/truncate large old tool responses if history exceeds the threshold
                    if (need_pruning && msg.role == "tool" && msg.msg_id != turn_id && !msg.msg_id.empty()) {
                        if (content_to_send.length() > 500) {
                            content_to_send = "[Conteudo da ferramenta '" + msg.name + "' ocultado (" + 
                                              std::to_string(content_to_send.length()) + 
                                              " caracteres) para economizar tokens. Chame a ferramenta novamente se precisar.]";
                        }
                    }
                    
                    if (!content_to_send.empty()) {
                        m["content"] = content_to_send;
                    } else if (msg.role == "assistant" && !msg.tool_calls.is_null()) {
                        m["content"] = nullptr; // OpenAI spec requires content to be null or empty string when tool_calls exist
                    } else {
                        m["content"] = "";
                    }
                    
                    if (!msg.name.empty()) m["name"] = msg.name;
                    if (!msg.tool_call_id.empty()) m["tool_call_id"] = msg.tool_call_id;
                    if (!msg.tool_calls.is_null()) m["tool_calls"] = msg.tool_calls;
                    
                    // Track size of what is actually sent
                    sent_chars += content_to_send.length();
                    
                    messages.push_back(m);
                }
                
                last_request_total_chars = total_chars;
                last_request_sent_chars = sent_chars;
                
                req["messages"] = messages;
            }
            req["model"] = model;
            req["temperature"] = temperature;
            if (max_tokens > 0) {
                req["max_tokens"] = max_tokens;
            }
            
            // Define tools
            json tools_array = json::array();
            // list_directory
            tools_array.push_back({
                {"type", "function"},
                {"function", {
                    {"name", "list_directory"},
                    {"description", "Lista arquivos e pastas em um caminho informado de forma recursiva ou nao. Retorna uma listagem de texto compacta com informacoes de tipo, caminho e tamanho."},
                    {"parameters", {
                        {"type", "object"},
                        {"properties", {
                            {"path", {{"type", "string"}, {"description", "Caminho absoluto ou relativo do diretorio a ser listado."}}},
                            {"recursive", {{"type", "boolean"}, {"description", "Se deve listar recursivamente (padrao e true)."}}}
                        }},
                        {"required", json::array({"path"})}
                    }}
                }}
            });
            // read_file
            tools_array.push_back({
                {"type", "function"},
                {"function", {
                    {"name", "read_file"},
                    {"description", "Le o conteudo completo de um arquivo em especifico."},
                    {"parameters", {
                        {"type", "object"},
                        {"properties", {
                            {"path", {{"type", "string"}, {"description", "Caminho absoluto ou relativo do arquivo a ser lido."}}}
                        }},
                        {"required", json::array({"path"})}
                    }}
                }}
            });
            // modify_file
            tools_array.push_back({
                {"type", "function"},
                {"function", {
                    {"name", "modify_file"},
                    {"description", "Modifica um arquivo substituindo um bloco de texto 'target_content' por 'replacement_content'. Se o arquivo nao existe e 'target_content' esta vazio, cria um novo arquivo."},
                    {"parameters", {
                        {"type", "object"},
                        {"properties", {
                            {"path", {{"type", "string"}, {"description", "Caminho do arquivo a ser modificado ou criado."}}},
                            {"target_content", {{"type", "string"}, {"description", "O bloco exato de texto a ser substituido no arquivo. Deve ser unico. Deixe vazio somente se estiver criando um novo arquivo."}}},
                            {"replacement_content", {{"type", "string"}, {"description", "O novo bloco de texto que substituira o anterior (ou o conteudo do novo arquivo)."}}}
                        }},
                        {"required", json::array({"path", "target_content", "replacement_content"})}
                    }}
                }}
            });
            
            req["tools"] = tools_array;
            req["tool_choice"] = "auto";
            
            std::string url = "http://" + host + ":" + std::to_string(port) + "/v1/chat/completions";
            std::string response;
            
            // Log for debugging (can be viewed in console)
            std::cout << "[API POST Request] Sending message history to: " << url << std::endl;
            
            if (!make_post_request(url, req.dump(), response, err_out)) {
                on_complete(false, "Falha na conexao ou tempo esgotado: " + err_out);
                return;
            }
            
            try {
                json res = json::parse(response);
                if (res.contains("error")) {
                    on_complete(false, "Erro do servidor LM Studio: " + res["error"]["message"].get<std::string>());
                    return;
                }
                
                if (!res.contains("choices") || res["choices"].empty()) {
                    on_complete(false, "Resposta vazia ou invalida do servidor.\n" + response);
                    return;
                }

                auto& choice = res["choices"][0];
                auto& res_msg = choice["message"];
                
                std::string content = "";
                if (res_msg.contains("content") && !res_msg["content"].is_null()) {
                    content = res_msg["content"].get<std::string>();
                }
                
                nlohmann::json tool_calls = nullptr;
                if (res_msg.contains("tool_calls") && !res_msg["tool_calls"].is_null()) {
                    tool_calls = res_msg["tool_calls"];
                }
                
                // Add assistant response to history
                {
                    std::lock_guard<std::mutex> lock(history_mutex);
                    Message assistant_msg;
                    assistant_msg.role = "assistant";
                    assistant_msg.content = content;
                    assistant_msg.tool_calls = tool_calls;
                    assistant_msg.timestamp = get_current_timestamp();
                    assistant_msg.msg_id = turn_id;
                    history.push_back(assistant_msg);
                }
                
                if (!tool_calls.is_null() && !tool_calls.empty()) {
                    on_status_change("Executando chamadas de ferramenta do modelo...");
                    if (!run_tool_calls_loop(tool_calls, on_status_change, err_out)) {
                        on_complete(false, "Erro ao processar as ferramentas: " + err_out);
                        return;
                    }
                    // Loop again with the tool response added to history
                } else {
                    final_content = content;
                    loop = false; // Finished!
                }
            } catch (const std::exception& e) {
                on_complete(false, std::string("Erro no parse JSON: ") + e.what() + "\nResposta do servidor: " + response);
                return;
            }
        }
        
        on_complete(true, final_content);
    }).detach();
}

bool LMClient::run_tool_calls_loop(const nlohmann::json& tool_calls, 
                                   std::function<void(const std::string& status)> on_status_change, 
                                   std::string& err_out) {
    for (const auto& call : tool_calls) {
        std::string call_id = call["id"].get<std::string>();
        std::string func_name = call["function"]["name"].get<std::string>();
        std::string args_str = call["function"]["arguments"].get<std::string>();
        
        on_status_change("Executando: " + func_name + "()");
        std::cout << "[Tool Run] Executing function: " << func_name << " with arguments: " << args_str << std::endl;
        
        std::string result = "";
        try {
            json args = json::parse(args_str);
            if (func_name == "list_directory") {
                std::string path = args["path"].get<std::string>();
                bool recursive = args.value("recursive", true);
                result = tools::list_directory(path, recursive);
            } else if (func_name == "read_file") {
                std::string path = args["path"].get<std::string>();
                result = tools::read_file(path);
            } else if (func_name == "modify_file") {
                std::string path = args["path"].get<std::string>();
                std::string target = args["target_content"].get<std::string>();
                std::string replacement = args["replacement_content"].get<std::string>();
                
                std::string modify_err = "";
                result = tools::modify_file(path, target, replacement, modify_err);
            } else {
                result = "Error: Unknown tool '" + func_name + "'";
            }
        } catch (const std::exception& e) {
            result = "Error parsing arguments: " + std::string(e.what());
        }
        
        // Add tool response to history
        {
            std::lock_guard<std::mutex> lock(history_mutex);
            Message tool_msg;
            tool_msg.role = "tool";
            tool_msg.content = result;
            tool_msg.name = func_name;
            tool_msg.tool_call_id = call_id;
            tool_msg.timestamp = get_current_timestamp();
            tool_msg.msg_id = tools::current_tool_message_id;
            history.push_back(tool_msg);
        }
    }
    return true;
}

std::vector<Message> LMClient::get_history() {
    std::lock_guard<std::mutex> lock(history_mutex);
    return history;
}

void LMClient::add_message(const Message& msg) {
    std::lock_guard<std::mutex> lock(history_mutex);
    history.push_back(msg);
}

void LMClient::clear_history() {
    std::lock_guard<std::mutex> lock(history_mutex);
    // Keep the system prompt if there is one
    Message sys_prompt;
    bool found_sys = false;
    for (const auto& msg : history) {
        if (msg.role == "system") {
            sys_prompt = msg;
            found_sys = true;
            break;
        }
    }
    history.clear();
    if (found_sys) {
        history.push_back(sys_prompt);
    }
}

void LMClient::set_system_prompt(const std::string& prompt) {
    std::lock_guard<std::mutex> lock(history_mutex);
    // Find and replace or insert at beginning
    for (auto it = history.begin(); it != history.end(); ++it) {
        if (it->role == "system") {
            it->content = prompt;
            return;
        }
    }
    // Insert at front
    Message sys;
    sys.role = "system";
    sys.content = prompt;
    sys.timestamp = get_current_timestamp();
    history.insert(history.begin(), sys);
}

} // namespace lm
