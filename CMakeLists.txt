cmake_minimum_required(VERSION 3.16)

project(lmstudio_chat_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Detecta automaticamente todos os .cpp
file(GLOB_RECURSE PROJECT_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
)

file(GLOB IMGUI_SOURCES
    CONFIGURE_DEPENDS
    thirdparty/imgui/*.cpp
)

set(IMGUI_BACKEND_SOURCES
    thirdparty/imgui/backends/imgui_impl_glfw.cpp
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp
)

add_executable(
    lmstudio_chat_client
    ${PROJECT_SOURCES}
    ${IMGUI_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
)

target_include_directories(lmstudio_chat_client PRIVATE
    include
    src
    thirdparty/imgui
    thirdparty/imgui/backends
)

find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(nlohmann_json REQUIRED)

target_link_libraries(lmstudio_chat_client PRIVATE
    CURL::libcurl
    OpenGL::GL
    glfw
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(lmstudio_chat_client PRIVATE gdi32 opengl32)
endif()