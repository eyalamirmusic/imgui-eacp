add_executable(MixedViews Main.cpp)
target_link_libraries(MixedViews PRIVATE imgui-eacp eacp-webview imgui_eacp_warnings)

set(BUNDLE_ID "com.imguieacp.mixedviews")

set_target_properties(MixedViews PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Mixed Views"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(MixedViews)
set_default_target_setting(MixedViews)
