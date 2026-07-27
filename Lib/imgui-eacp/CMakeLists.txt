add_library(imgui-eacp STATIC)

target_sources(imgui-eacp PRIVATE
        Input/KeyMap.cpp
        Renderer/DrawRenderer.cpp
        View/ImGuiView.cpp)

# The include root is Lib/, so app code spells its includes
# <imgui-eacp/ImGuiEacp.h> — the same shape as eacp's own <eacp/GPU/GPU.h>.
target_include_directories(imgui-eacp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# eacp-sprites rather than eacp-gpu: it pulls the whole GPU stack in and adds
# the sprite renderer, which the mixed example draws its backdrop with.
target_link_libraries(imgui-eacp
        PUBLIC eacp-sprites imgui::imgui
        PRIVATE imgui_eacp_warnings)

add_ide_sources(imgui-eacp)
