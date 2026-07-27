add_executable(Demo Main.cpp)
target_link_libraries(Demo PRIVATE imgui-eacp imgui_eacp_warnings)

set(BUNDLE_ID "com.imguieacp.demo")

set_target_properties(Demo PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ImGui Demo"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(Demo)
set_default_target_setting(Demo)
