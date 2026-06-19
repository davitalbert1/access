cmake_minimum_required(VERSION 3.16)
project(lmstudio_chat_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
    src/tools.cpp
    src/lm_client.cpp
    src/diff_viewer.cpp
    src/chat_app.cpp
    
    # ImGui sources
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/backends/imgui_impl_glfw.cpp
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp
)

# Headers
set(HEADERS
    src/tools.hpp
    src/lm_client.hpp
    src/diff_viewer.hpp
    src/chat_app.hpp
)

add_executable(lmstudio_chat_client ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(lmstudio_chat_client PRIVATE
    src
    thirdparty/imgui
    thirdparty/imgui/backends
)

# Find Packages
find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)

# For GLFW, look for config or standard package
find_package(glfw3 CONFIG)
if(NOT glfw3_FOUND)
    find_package(glfw REQUIRED)
endif()

# For nlohmann_json
find_package(nlohmann_json CONFIG REQUIRED)

# Link Libraries
target_link_libraries(lmstudio_chat_client PRIVATE
    CURL::libcurl
    OpenGL::GL
    nlohmann_json::nlohmann_json
)

if(glfw3_FOUND)
    target_link_libraries(lmstudio_chat_client PRIVATE glfw)
else()
    target_link_libraries(lmstudio_chat_client PRIVATE glfw)
endif()

# Additional windows libraries needed for GLFW/OpenGL
if(WIN32)
    target_link_libraries(lmstudio_chat_client PRIVATE gdi32)
endif()
