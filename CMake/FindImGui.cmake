include(CPM)

# Dear ImGui ships no CMakeLists of its own — it is meant to be compiled into
# the host project — so the fetch is DOWNLOAD_ONLY and the target is built here.
CPMAddPackage(
        NAME ImGui
        GITHUB_REPOSITORY ocornut/imgui
        GIT_TAG v1.92.5
        DOWNLOAD_ONLY YES)

if (ImGui_ADDED AND NOT TARGET imgui)
    add_library(imgui STATIC
            "${ImGui_SOURCE_DIR}/imgui.cpp"
            "${ImGui_SOURCE_DIR}/imgui_draw.cpp"
            "${ImGui_SOURCE_DIR}/imgui_tables.cpp"
            "${ImGui_SOURCE_DIR}/imgui_widgets.cpp"

            # ImGui::ShowDemoWindow lives here. It is the single best smoke test
            # a renderer backend has — it exercises every widget, every clip
            # rect and the whole font atlas — so it is always compiled in.
            "${ImGui_SOURCE_DIR}/imgui_demo.cpp")

    # SYSTEM so ImGui's own headers never trip this project's -Wall -Wextra
    # -Wpedantic, which it does not build clean under.
    target_include_directories(imgui SYSTEM PUBLIC "${ImGui_SOURCE_DIR}")
    target_compile_features(imgui PUBLIC cxx_std_20)

    set_target_properties(imgui PROPERTIES FOLDER Dependencies)

    add_library(imgui::imgui ALIAS imgui)
endif ()
