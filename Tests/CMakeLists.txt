set(CMAKE_FOLDER Tests)

find_package(NanoTest REQUIRED)

# Everything that needs no GPU device: the input mapping tables, and the
# properties of the vertex the renderer builds. These are the cases that would
# run anywhere, and they are kept separate from the rendered ones for the same
# reason eacp's Mesh suite is — a failure here is a failure in this repository's
# own logic, not in a driver.
set(imgui_eacp_test_sources
        KeyMapTests.cpp
        TestMain.cpp)

# The rendered cases ride View::renderToImage over the ImGuiView itself: Metal
# off-screen textures on Apple, D3D12 off-screen render targets on Windows. That
# is the same off-screen path eacp's whole GPU suite uses, and it is what makes
# a claim about this backend checkable on Windows rather than only compilable
# there. Each self-skips when there is no device.
if (APPLE OR WIN32)
    list(APPEND imgui_eacp_test_sources
            DrawGeometryTests.cpp
            ContextTests.cpp)
endif ()

nano_add_executable(ImGuiEacpTests
        NO_MAIN
        SOURCES
            ${imgui_eacp_test_sources}
        TARGETS imgui-eacp)
