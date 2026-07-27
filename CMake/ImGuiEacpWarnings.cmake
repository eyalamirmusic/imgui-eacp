# Named ImGuiEacpWarnings rather than Warnings because a host project's
# CMAKE_MODULE_PATH is searched before its dependencies'. EACP and Miro both
# `include(Warnings)` from their own CMake/ directory, and a file of that name
# here would shadow theirs.
add_library(imgui_eacp_warnings INTERFACE)

if (MSVC)
    target_compile_options(imgui_eacp_warnings INTERFACE /W4)
else ()
    target_compile_options(imgui_eacp_warnings INTERFACE
            -Wall -Wextra -Wpedantic)
endif ()
