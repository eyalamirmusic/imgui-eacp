add_executable(Bench Main.cpp FrameStats.cpp Workload.cpp)
target_link_libraries(Bench PRIVATE imgui-eacp imgui_eacp_warnings)

set(BUNDLE_ID "com.imguieacp.bench")

set_target_properties(Bench PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ImGui Bench"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(Bench)
set_default_target_setting(Bench)
