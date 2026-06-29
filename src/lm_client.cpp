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
#include <regex>
#include <string>
#include <string_view>

static std::mutex log_file_mutex;
thread_local std::string reasoning_accum;

static void log_tool_call(const std::string& func_name, const std::string& args_str, const std::string& result) {
    std::lock_guard<std::mutex> lock(log_file_mutex);
    static std::ofstream log_file("tool_calls.log", std::ios::app);
    if (!log_file.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ts;
    ts << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");

    log_file << "[" << ts.str() << "] TOOL CALL | func=" << func_name << " | args=" << args_str << " | result=" << result << std::endl;
    log_file.flush();
}

static std::atomic<int> global_msg_id_counter{1};

using json = nlohmann::json;

namespace lm {
    static bool contains(std::string_view text, std::string_view word) {
        return text.find(word) != std::string_view::npos;
    }

    static bool prompt_requires_tool_use(const std::string& prompt) {
        std::string lower = prompt;

        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

        int score = 0;

        static const std::string_view action_words[] = { "ler", "listar", "mostrar", "abrir",
            "editar", "modificar", "alterar", "criar", "remover", "deletar", "analisar",
            "explorar", "inspecionar", "verificar"};
        static const std::string_view file_words[] = {"arquivo", "arquivos", "file", "files"};
        static const std::string_view project_words[] = {"pasta", "pastas", "diretorio", "diretorios",
            "diretório", "diretórios", "folder", "folders", "directory", "directories", "caminho",
            "caminhos", "path", "paths", "codigo", "código", "fonte", "source", "projeto", "projetos",
            "workspace", "repositorio", "repositório", "repository"};

        for (auto word : action_words) {
            if (contains(lower, word)) score += 2;
        }
        for (auto word : file_words) {
            if (contains(lower, word)) score += 3;
        }
        for (auto word : project_words) {
            if (contains(lower, word)) score += 3;
        }

        // Arquivos comuns de projetos
        static const std::regex project_refs(
            R"((\.cpp|\.hpp|\.h|\.c|\.cc|\.cxx|\.py|\.js|\.ts|\.tsx|\.java|\.cs|\.go|\.rs|\.php|\.json|\.xml|\.yaml|\.yml|\.toml|\.ini|cmakelists\.txt|package\.json|cargo\.toml|makefile|dockerfile|readme\.md|src/|include/))",
            std::regex_constants::icase);

        if (std::regex_search(lower, project_refs)) score += 5;

        // Caminhos típicos
        static const std::regex path_pattern( R"(([a-z]:\\|\/home\/|\/usr\/|\/opt\/|\.\/|\.\.\/|\\))", std::regex_constants::icase);

        if (std::regex_search(lower, path_pattern)) score += 5;

        return score >= 3;
    }

    static std::string format_list_directory_compact(const std::string& json_str) {
        try {
            json res = json::parse(json_str);
            if (res.contains("error")) return "erro: " + res["error"].get<std::string>();
            if (!res.contains("files") || !res["files"].is_array()) return json_str;

            const auto& files = res["files"];
            std::stringstream ss;
            ss << "dir(" << files.size() << "):\n";

            for (const auto& file : files) {
                std::string path = file.value("p", "");
                bool is_dir = file.value("d", false);
                if (is_dir) {
                    ss << "d " << path << "\n";
                } else {
                    ss << "f " << path << " (" << file.value("s", 0) << " bytes)";
                    if (file.contains("m") && !file.value("m", "").empty())
                        ss << " " << file.value("m", "");
                    ss << "\n";
                }
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
                if (item.value("d", false)) {
                    ss << "d " << item.value("p", "") << "\n";
                } else {
                    ss << "f " << item.value("p", "") << " (" << item.value("s", 0) << " bytes)\n";
                }
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
            ss << res.value("p", "") << " | " << (res.value("is_directory", false) ? "dir" : "file") << " | " << res.value("size", 0);
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
        "You are a code analysis agent.\n"
        "Reply in Portuguese.\n"
        "\n"
        "STRICT RULES:\n"
        "- Never output XML.\n"
        "- Never output <tool_call>.\n"
        "- Never output markdown describing tools.\n"
        "- Tool calls MUST be returned ONLY using the OpenAI tool_calls field.\n"
        "- If a tool is needed, leave content empty and populate tool_calls.\n"
        "- If no tool is needed, answer normally.\n"
        "- Any XML output is invalid.\n";

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
            reasoning_accum.clear();
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
            bool completion_signaled = false;

            // Cache estático de tools (evita rebuild a cada request)
            static json cached_tools;

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

                if (!cached_tools.is_array() || cached_tools.empty()) {
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
                                {"properties", {
                                    {"path", {{"type", "string"}}},
                                    {"chunk_size", {{"type", "integer"}}},
                                    {"offset", {{"type", "integer"}}}
                                }},
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
                req["tool_choice"] = "auto";
                req["parallel_tool_calls"] = false;

                std::string url = "http://" + host + ":" + std::to_string(port) + "/v1/chat/completions";

                std::string response;

                std::cout << "[API] POST " << url << std::endl;

                if (!make_post_request(url, req.dump(), response, err_out)) {
                    on_complete(false, "Falha na conexao: " + err_out);
                    completion_signaled = true;
                    return;
                }

                try {
                    json res = json::parse(response);

                    if (res.contains("error")) {
                        std::string err_msg = res["error"]["message"].get<std::string>();
                        
                        // Detecta estouro de contexto: tenta podar e retentar
                        if (err_msg.find("context") != std::string::npos ||
                            err_msg.find("length") != std::string::npos ||
                            err_msg.find("token") != std::string::npos ||
                            err_msg.find("maximum") != std::string::npos) {
                            on_status_change("Contexto excedido - otimizando...");
                            // Poda: remove 1/3 do historico de tool calls mais antigos
                            {
                                std::lock_guard<std::mutex> lock(history_mutex);
                                size_t removed = 0;
                                size_t target = history.size() / 3;
                                auto it = history.begin();
                                while (it != history.end() && removed < target) {
                                    if (it->role == "tool" || (it->role == "assistant" && !it->tool_calls.is_null())) {
                                        it = history.erase(it);
                                        removed++;
                                    } else {
                                        ++it;
                                    }
                                }
                                std::cout << "[Context Pruning] Removidos " << removed << " turnos antigos para liberar contexto." << std::endl;
                            }
                            continue; // Tenta novamente com contexto reduzido
                        }

                        on_complete(false, "Erro do modelo: " + err_msg);
                        completion_signaled = true;
                        return;
                    }

                    auto& choice = res["choices"][0];
                    auto& msg = choice["message"];

                    std::cout << "\n=== MODEL OUTPUT DEBUG ===\n";
                    if (msg.contains("content")) std::cout << "content: " << msg["content"] << "\n";
                    if (msg.contains("reasoning_content")) std::cout << "reasoning_content: " << msg["reasoning_content"] << "\n";
                    if (msg.contains("tool_calls")) std::cout << "tool_calls: " << msg["tool_calls"].dump(2) << "\n";

                try {
                        if (msg.contains("reasoning_content") && msg["reasoning_content"].is_string()) {
                            std::string rc = msg["reasoning_content"].get<std::string>();
                            reasoning_accum += rc;
                        } else if (msg.contains("reasoning") && msg["reasoning"].is_string()) {
                            std::string rc = msg["reasoning"].get<std::string>();
                            reasoning_accum += rc;
                        } else if (msg.contains("reasoning_tokens")) {
                            reasoning_accum += msg["reasoning_tokens"].dump();
                        }
                    }
                    catch (...) {
                        // optional safety
                    }

                    std::string content;
                    if (msg.contains("content") && !msg["content"].is_null()) content = msg["content"].get<std::string>();

                    json tool_calls = nullptr;
                    bool has_tool_calls_from_json = msg.contains("tool_calls") && msg["tool_calls"].is_array() && !msg["tool_calls"].empty();

                    if (has_tool_calls_from_json) {
                        content = "[tool use]";
                        tool_calls = msg["tool_calls"];
                    } else {
                        // Fallback: tenta extrair tool_call XML do reasoning ou content
                        // Formato esperado (qwen): <tool_call>\n<function=funcName>\n<parameter=key>value</parameter>\n</function>\n</tool_call>
                        std::string combined = reasoning_accum + "\n" + content;
                        std::regex xml_tool_call(R"(<tool_call>\s*<function=(\w+)>([\s\S]*?)</function>\s*</tool_call>)");
                        std::smatch match;
                        if (std::regex_search(combined, match, xml_tool_call)) {
                            std::string func_name = match[1].str();
                            std::string args_xml = match[2].str();
                            
                            // Extrai parametros do XML <parameter=key>value</parameter>
                            json args = json::object();
                            std::regex param_regex(R"(<parameter=(\w+)>([\s\S]*?)</parameter>)");
                            auto param_begin = std::sregex_iterator(args_xml.begin(), args_xml.end(), param_regex);
                            auto param_end = std::sregex_iterator();
                            for (auto it = param_begin; it != param_end; ++it) {
                                std::string key = (*it)[1].str();
                                std::string val = (*it)[2].str();
                                // Trim whitespace
                                val.erase(0, val.find_first_not_of(" \n\r\t"));
                                val.erase(val.find_last_not_of(" \n\r\t") + 1);
                                // Tenta detectar boolean
                                if (val == "true") args[key] = true;
                                else if (val == "false") args[key] = false;
                                else args[key] = val;
                            }
                            
                            json call = json::object();
                            call["id"] = "xml_" + std::to_string(global_msg_id_counter.load());
                            call["type"] = "function";
                            call["function"] = json::object();
                            call["function"]["name"] = func_name;
                            call["function"]["arguments"] = args.dump();
                            
                            json arr = json::array();
                            arr.push_back(call);
                            tool_calls = arr;
                            content = "[tool use]";
                            std::cout << "[XML Fallback] Parsed tool call: " << func_name << " args=" << args.dump() << std::endl;
                        }
                    }

                    std::cout << "\n=== RAW MODEL RESPONSE ===\n";
                    std::cout << response << std::endl;

                    // salva assistant msg
                    {
                        std::lock_guard<std::mutex> lock(history_mutex);

                        Message assistant_msg;
                        assistant_msg.role = "assistant";
                        if (msg.contains("tool_calls") && !msg["tool_calls"].is_null()) {
                            assistant_msg.content = "";
                        } else {
                            assistant_msg.content = content;
                        }
                        assistant_msg.tool_calls = tool_calls;
                        assistant_msg.timestamp = get_current_timestamp();
                        assistant_msg.msg_id = turn_id;
                        assistant_msg.reasoning = reasoning_accum;
                        history.push_back(assistant_msg);
                    }

                    // Guard contra loop infinito de ferramentas (usando thread_local para persistir entre iterações)
                    static thread_local int tool_loop_counter = 0;

                    // TOOL LOOP
                    if (!tool_calls.is_null() && !tool_calls.empty()) {
                        on_status_change("Executando ferramentas...");

                        if (!run_tool_calls_loop(tool_calls, on_status_change, err_out)) {
                            on_complete(false, err_out);
                            completion_signaled = true;
                            return;
                        }

                        tool_loop_counter++;
                        if (tool_loop_counter > 15) {
                            on_complete(false, "Loop infinito de ferramentas detectado (15+ ciclos).");
                            completion_signaled = true;
                            return;
                        }
                    } else {
                        // Reset contador quando modelo finalmente responde sem tools
                        tool_loop_counter = 0;
                        final_content = content;
                        loop = false;
                    }
                } catch (const std::exception& e) {
                    on_complete(false, std::string("Erro JSON: ") + e.what());
                    completion_signaled = true;
                    return;
                }
            }

            if (!completion_signaled) {
                std::string final_output = final_content;
                std::string reasoning_final = reasoning_accum;
                if (!reasoning_final.empty()) final_output += "\n\n[REASONING]\n" + reasoning_final;
                on_complete(true, final_output);
            }

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
                    size_t chunk_size = args.value("chunk_size", 8192);
                    size_t offset = args.value("offset", 0);
                    result = tools::read_file(path, chunk_size, offset);
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