#!/bin/bash
set -e

echo "=== Compilando lmstudio_chat_client ==="

g++ -std=c++17 -O2 \
    src/main.cpp \
    src/tools.cpp \
    src/lm_client.cpp \
    src/diff_viewer.cpp \
    src/chat_app.cpp \
    thirdparty/imgui/imgui.cpp \
    thirdparty/imgui/imgui_draw.cpp \
    thirdparty/imgui/imgui_tables.cpp \
    thirdparty/imgui/imgui_widgets.cpp \
    thirdparty/imgui/backends/imgui_impl_glfw.cpp \
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp \
    -Isrc \
    -Ithirdparty/imgui \
    -Ithirdparty/imgui/backends \
    $(pkg-config --cflags --libs glfw3 libcurl nlohmann_json) \
    -lopengl32 -lgdi32 \
    -o lmstudio_chat_client.exe

echo "=== Pronto! Execute: ./lmstudio_chat_client.exe ==="
