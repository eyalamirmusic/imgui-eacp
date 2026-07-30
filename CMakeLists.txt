cmake_minimum_required(VERSION 3.31)
project(imgui-eacp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (APPLE)
    enable_language(OBJCXX)

    # CMake already puts -lobjc on the link line of everything that pulls in
    # Objective-C, and Xcode adds -fobjc-link-runtime on top for any target with
    # its own .mm sources; ld warns about the duplicate. Mirrors eacp's own root.
    set(CMAKE_XCODE_ATTRIBUTE_CLANG_LINK_OBJC_RUNTIME NO)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(CPM)
include(ImGuiEacpWarnings)

# eacp builds its Graphics and GPU modules on Apple and Windows only, and this
# library is a GPUView — so say that here rather than letting the configure fail
# on a missing eacp-sprites target further down.
if (NOT APPLE AND NOT WIN32)
    message(FATAL_ERROR
            "imgui-eacp needs eacp's GPU module, which builds on macOS, iOS and "
            "Windows. This platform is not one of them.")
endif ()

find_package(EACP REQUIRED)
find_package(ImGui REQUIRED)

add_subdirectory(Lib)

option(IMGUI_EACP_ENABLE_EXAMPLES "Build the imgui-eacp example apps"
        ${PROJECT_IS_TOP_LEVEL})

if (IMGUI_EACP_ENABLE_EXAMPLES)
    add_subdirectory(Apps)
endif ()

# Off when this library is somebody's subproject, the same way eacp's own
# EACP_ENABLE_TESTS is: a project consuming imgui-eacp should not find this
# repository's test targets, or NanoTest, in its build tree.
option(IMGUI_EACP_ENABLE_TESTS "Build the imgui-eacp tests"
        ${PROJECT_IS_TOP_LEVEL})

if (IMGUI_EACP_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()
