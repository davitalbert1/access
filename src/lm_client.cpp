#include "../include/lm_client.h"
#include "../include/tools.h"
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cctype>

static std::mutex log_file_mutex;

static void log_tool_call(const std::string& func_name, const std::string& args_str, const std::string& result) {
    std::lock_guard<std::mutex> lock(log_file_mutex);
    static std::ofstream log_file("tool_calls.log", std::ios::app);
    if (!log_file.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ts;
    ts << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");

    log_file << "[" << ts.str() << "] TOOL CALL | func=" << func_name
             << " | args=" << args_str
             << " | result=" << result << std::endl;
    log_file.flush();
}

static std::atomic<int> global_msg_id_counter{1};

using json = nlohmann::json;

namespace lm {
static bool prompt_requires_tool_use(const std::string& prompt) {
    std::string lower = prompt;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    static const char* markers[] = {
        "arquivo", "arquivos", "pasta", "pastas", "diretório", "diretórios", "caminho", "caminhos",
        "codigo", "código", "projeto", "projetos", "ler", "listar", "mostrar", "editar",
        "modificar", "criar", "explorar", "conteudo", "conteúdo", "file", "files", "folder",
        "folders", "directory", "directories", "path", "paths", "code", "project", "src", "include"
    };

    for (const char* marker : markers) {
        if (lower.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static std::string format_list_directory_compact(const std::string& json_str) {
    try {
        json res = json::parse(json_str);

        if (res.contains("error")) return "erro: " + res["error"].get<std::string>();
        if (!res.contains("files") || !res["files"].is_array()) return json_str;

        const auto& files = res["files"];

        std::stringstream ss;

        // Cabeçalho ultra curto
        ss << "dir(" << files.size() << "):\n";

        for (const auto& file : files) {
            std::string path = file.value("path", "");
            bool is_dir = file.value("is_directory", false);

            ss << (is_dir ? "d " : "f ") << path;

            // tamanho só se arquivo
            if (!is_dir) ss << " " << file.value("size", 0);

            ss << "\n";
        }

        if (res.value("truncated", false)) ss << "...trunc\n";

        return ss.str();

    } catch (...) {
        return json_str;
    }
}

static std::string format_search_results_compact(const std::string& json_str) {
    try {
        json res = json::parse(json_str);
        if (res.contains("error")) return "erro: " + res["error"].get<std::string>();
        if (!res.contains("matches") || !res["matches"].is_array()) return json_str;

        std::stringstream ss;
        ss << "search(" << res.value("count", 0) << "):\n";
        for (const auto& item : res["matches"]) {
            ss << (item.value("is_directory", false) ? "d " : "f ") << item.value("path", "") << "\n";
        }
        return ss.str();
    } catch (...) {
        return json_str;
    }
}

static std::string format_file_info_compact(const std::string& json_str) {
    try {
        json res = json::parse(json_str);
        if (res.contains("error")) return "erro: " + res["error"].get<std::string>();
        if (!res.value("exists", false)) return "nao existe";
        std::stringstream ss;
        ss << res.value("name", "") << " | " << (res.value("is_directory", false) ? "dir" : "file") << " | " << res.value("size", 0);
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
    custom_system_prompt =
    "Você é um assistente de programação altamente confiável, com comportamento estilo Antigravity: primeiro inspecione o ambiente local, depois execute a ação mínima necessária e só então responda.\n\n"

    "REGRAS FUNDAMENTAIS (OBRIGATÓRIAS):\n"
    "1. Nunca invente conteúdo de arquivos, diretórios ou sistema local.\n"
    "2. Se a tarefa envolver arquivos, pastas, código, projeto ou caminhos, use ferramentas imediatamente.\n"
    "3. Nunca responda com suposições como 'provavelmente' ou 'acho que'.\n"
    "4. Sempre que houver dados reais do sistema, use ferramentas antes de responder.\n\n"

    "USO OBRIGATÓRIO DE FERRAMENTAS:\n"
    "- Para listar diretórios ou arquivos: use 'list_directory'\n"
    "- Para ler conteúdo de arquivos: use 'read_file'\n"
    "- Para pesquisar arquivos: use 'search_files'\n"
    "- Para obter metadados de um arquivo: use 'get_file_info'\n"
    "- Para modificar arquivos: use 'modify_file'\n"
    "- Prefira a ferramenta mais barata e direta que resolva a necessidade.\n\n"

    "COMPORTAMENTO:\n"
    "- Se a tarefa pedir dados locais, chame pelo menos uma ferramenta antes de responder.\n"
    "- Quando houver uma ferramenta para obter a informação, não responda diretamente.\n"
    "- Use os resultados das ferramentas como fonte primária de verdade.\n"
    "- Responda em português.\n";
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
                if (model == "default" || model.empty()) model = first_model;
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

        // Add user message
        {
            std::lock_guard<std::mutex> lock(history_mutex);
            history.push_back({"user", user_prompt, "", "", nullptr, get_current_timestamp(), turn_id});
        }

        on_status_change("Enviando prompt ao modelo...");

        bool loop = true;
        std::string final_content;
        std::string err_out;
        bool force_tool_use = prompt_requires_tool_use(user_prompt);

        // Cache estático de tools (evita rebuild a cada request)
        static json cached_tools = nullptr;

        while (loop) {
            json req = json::object();

            {
                std::lock_guard<std::mutex> lock(history_mutex);

                json messages = json::array();

                if (force_tool_use) {
                    json forced_system;
                    forced_system["role"] = "system";
                    forced_system["content"] = "Modo obrigatório: esta solicitação precisa de dados reais do sistema. Chame pelo menos uma ferramenta antes de responder e use o resultado como fonte de verdade.";
                    messages.push_back(forced_system);
                }

                size_t total_chars = 0;
                for (const auto& msg : history) {
                    if (msg.role != "system_info") total_chars += msg.content.size();
                }

                bool need_pruning = total_chars > pruning_threshold;

                for (const auto& msg : history) {
                    if (msg.role == "system_info") continue;

                    json m;
                    m["role"] = msg.role;

                    std::string content = msg.content;

                    // Compacta saídas de ferramentas para economizar tokens
                    if (msg.role == "tool" && msg.name == "list_directory") {
                        content = format_list_directory_compact(content);
                    } else if (msg.role == "tool" && msg.name == "search_files") {
                        content = format_search_results_compact(content);
                    } else if (msg.role == "tool" && msg.name == "get_file_info") {
                        content = format_file_info_compact(content);
                    }

                    // Pruning agressivo de tool outputs antigos
                    if (need_pruning && msg.role == "tool" && msg.msg_id != turn_id && content.size() > 600) {
                        content = "[tool output omitido para economizar tokens: " + msg.name + "]";
                    }

                    // IMPORTANTE: nunca enviar vazio
                    if (!content.empty()) m["content"] = content;

                    // NÃO enviar metadata desnecessária
                    if (msg.role == "tool" && !msg.tool_call_id.empty()) m["tool_call_id"] = msg.tool_call_id;

                    // tool_calls só no assistant
                    if (msg.role == "assistant" && !msg.tool_calls.is_null()) m["tool_calls"] = msg.tool_calls;

                    messages.push_back(m);
                }

                req["messages"] = messages;
            }

            req["model"] = model;
            req["temperature"] = temperature;
            if (max_tokens > 0) req["max_tokens"] = max_tokens;

            if (cached_tools.is_null()) {
                cached_tools = json::array();

                cached_tools.push_back({
                    {"type", "function"},
                    {"function", {
                        {"name", "list_directory"},
                        {"description", "Lista diretórios"},
                        {"parameters", {
                            {"type", "object"},
                            {"properties", {
                                {"path", {{"type", "string"}}},
                                {"recursive", {{"type", "boolean"}}}
                            }},
                            {"required", json::array({"path"})}
                        }}
                    }}
                });

                cached_tools.push_back({
                    {"type", "function"},
                    {"function", {
                        {"name", "read_file"},
                        {"description", "Lê arquivo"},
                        {"parameters", {
                            {"type", "object"},
                            {"properties", {{"path", {{"type", "string"}}}}},
                            {"required", json::array({"path"})}
                        }}
                    }}
                });

                cached_tools.push_back({
                    {"type", "function"},
                    {"function", {
                        {"name", "search_files"},
                        {"description", "Pesquisa arquivos por nome"},
                        {"parameters", {
                            {"type", "object"},
                            {"properties", {
                                {"path", {{"type", "string"}}},
                                {"pattern", {{"type", "string"}}},
                                {"recursive", {{"type", "boolean"}}},
                                {"max_results", {{"type", "integer"}}}
                            }},
                            {"required", json::array({"path"})}
                        }}
                    }}
                });

                cached_tools.push_back({
                    {"type", "function"},
                    {"function", {
                        {"name", "get_file_info"},
                        {"description", "Obtém metadados de um arquivo ou pasta"},
                        {"parameters", {
                            {"type", "object"},
                            {"properties", {{"path", {{"type", "string"}}}}},
                            {"required", json::array({"path"})}
                        }}
                    }}
                });

                cached_tools.push_back({
                    {"type", "function"},
                    {"function", {
                        {"name", "modify_file"},
                        {"description", "Modifica arquivo"},
                        {"parameters", {
                            {"type", "object"},
                            {"properties", {
                                {"path", {{"type", "string"}}},
                                {"target_content", {{"type", "string"}}},
                                {"replacement_content", {{"type", "string"}}}
                            }},
                            {"required", json::array({"path", "target_content", "replacement_content"})}
                        }}
                    }}
                });
            }

            req["tools"] = cached_tools;
            req["tool_choice"] = force_tool_use ? "required" : "auto";
            req["parallel_tool_calls"] = false;

            std::string url = "http://" + host + ":" + std::to_string(port) + "/v1/chat/completions";

            std::string response;

            std::cout << "[API] POST " << url << std::endl;

            if (!make_post_request(url, req.dump(), response, err_out)) {
                on_complete(false, "Falha na conexao: " + err_out);
                return;
            }

            try {
                json res = json::parse(response);

                if (res.contains("error")) {
                    on_complete(false, res["error"]["message"].get<std::string>());
                    return;
                }

                auto& choice = res["choices"][0];
                auto& msg = choice["message"];

                std::string content;
                if (msg.contains("content") && !msg["content"].is_null()) content = msg["content"].get<std::string>();

                json tool_calls = nullptr;
                if (msg.contains("tool_calls") && !msg["tool_calls"].is_null()) tool_calls = msg["tool_calls"];

                // salva assistant msg
                {
                    std::lock_guard<std::mutex> lock(history_mutex);

                    history.push_back({"assistant", content, "", "", tool_calls, get_current_timestamp(), turn_id});
                }

                // TOOL LOOP
                if (!tool_calls.is_null() && !tool_calls.empty()) {
                    on_status_change("Executando ferramentas...");

                    if (!run_tool_calls_loop(tool_calls, on_status_change, err_out)) {
                        on_complete(false, err_out);
                        return;
                    }

                } else {
                    final_content = content;
                    loop = false;
                }
            } catch (const std::exception& e) {
                on_complete(false, std::string("Erro JSON: ") + e.what());
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
            } else if (func_name == "search_files") {
                std::string path = args["path"].get<std::string>();
                std::string pattern = args.value("pattern", "");
                bool recursive = args.value("recursive", true);
                int max_results = args.value("max_results", 100);
                result = tools::search_files(path, pattern, recursive, max_results);
            } else if (func_name == "get_file_info") {
                std::string path = args["path"].get<std::string>();
                result = tools::get_file_info(path);
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

        log_tool_call(func_name, args_str, result);

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
    if (found_sys) history.push_back(sys_prompt);
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
}
