# Access - LM Studio Chat Client & Tools (Antigravity Style)

A lightweight, modern graphical user interface (GUI) developed in **C++** using **Dear ImGui**, **GLFW**, and **OpenGL 3**. This client connects to a local **LM Studio** server instance (via OpenAI-compatible API) to provide an intelligent, agentic coding assistant capable of executing local actions directly on your machine.

The visual design and interactions are inspired by the *Antigravity* style, providing a clean, dark, and professional workspace environment optimized for productivity.

---

## 🚀 Key Features

* **Agentic Developer Assistant:** The local AI model can interact directly with your workspace files via built-in tools:
  * `list_directory`: Lists directory structures and files recursively or flat.
  * `read_file`: Reads files in chunks instead of loading full content at once, using chunk_size and offset for paginated access to large files.
  * `modify_file`: Performs precise, single-occurrence search-and-replace edits or file creation.
* **Change History & Rollback:** Every file modification made by the assistant is logged in the side panel. You can inspect changes in a side-by-side diff window and revert any change with a single click.
* **Integrated File Browser:** Browse project files and directories directly within the application and view their contents in real-time.
* **Config Persistence:** Automatically saves your connection settings (host, port, model name) in a local configuration file (`config.json`).
* **Premium Custom Theme:** Features a dark-themed, minimalist IDE layout with responsive panels and floating windows.

---

## 🛠️ Prerequisites

To compile and run this project, you will need the following dependencies installed on your system:

* A **C++17** compatible compiler (e.g., GCC/G++ MinGW-w64 on Windows, or GCC/Clang on Linux/macOS).
* **CMake** (version 3.16 or higher) or **GNU Make**.
* **libcurl** (for sending HTTP requests to the LM Studio API).
* **GLFW3** (for window creation and input handling).
* **nlohmann/json** (for parsing configuration files and API responses).
* Graphics drivers supporting **OpenGL 3.0** or higher.

---

## 🏗️ Build and Run

### Method 1: Using the CMake Build Script (Recommended on Windows with MSYS2/MinGW)

1. Run the provided compile script:
   ```bash
   bash compilar.sh
   ```
2. The script cleans any previous build caches, runs CMake with the `"MSYS Makefiles"` generator, compiles the project, and places the executable in the root directory.
3. Launch the application:
   ```bash
   ./lmstudio_chat_client.exe
   ```

### Method 2: Direct Compilation via G++

If you have all dependencies globally installed and configured via `pkg-config`, you can compile directly using:
```bash
bash build.sh
```

### Method 3: Manual Compilation with CMake (MinGW)

```bash
rm -f CMakeCache.txt
rm -rf CMakeFiles
cmake -G "MinGW Makefiles" .
mingw32-make
./lmstudio_chat_client.exe
```

---

## 🤖 Configuring with LM Studio

1. Launch **LM Studio** on your machine.
2. Navigate to the **Developer Section / Local Server** tab (server icon in the sidebar).
3. Select and load a model that supports **Tool Calling / Function Calling** (models like `qwen2.5-coder` or similar are highly recommended for the agentic tools to work correctly).
4. Set the port (default is `1234`) and click **Start Server**.
5. In the **Access Client**, enter your `Host`, `Port`, and the loaded `Model` name (the client will check the connection and display a green indicator when active).
6. Send prompt requests like: *"Create a python script sorting_algorithms.py with bubble sort"* or *"Read src/main.cpp and explain the event loop"*.

---

## 📂 Project Structure

```text
├── include/
│   ├── chat_app.h            # Header for main UI and panels
│   ├── lm_client.h           # Header for API integration client
│   ├── tools.h               # Header for local filesystem tools
│   └── diff_viewer.h         # Header for side-by-side diff visualizer
├── src/
│   ├── main.cpp              # Entry point, initializes GLFW and ImGui
│   ├── chat_app.cpp          # Implementation of the main UI and chat panels
│   ├── lm_client.cpp         # API integration client and tool loop implementation
│   ├── tools.cpp             # Local file system tools exposed to the model
│   └── diff_viewer.cpp       # Basic side-by-side file diff visualizer
├── thirdparty/
│   └── imgui/                # Dear ImGui codebase and backend files (GLFW/OpenGL3)
├── CMakeLists.txt            # CMake build configuration script
├── Makefile                  # Helper makefile
├── build.sh                  # Shell script for direct G++ compilation
├── compilar.sh               # Main build script for Windows environments
├── git.md                    # Git setup and command reference sheet
└── config.json               # Config file for storing LM Studio connection options
```

---

## 📝 License and Contributions

This project is built for educational and productivity purposes in exploring local LLMs with tool capabilities. Feel free to open issues or submit Pull Requests to add new tools or improve the user interface!
