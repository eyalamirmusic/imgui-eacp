add_executable(Model Main.cpp Camera.cpp Inspector.cpp SampleModel.cpp Viewport.cpp)

# eacp-mesh is the loader and the renderer; imgui-eacp is the overlay over them.
# The two are linked side by side rather than one through the other, because
# nothing in the integration library knows the Mesh module exists.
target_link_libraries(Model PRIVATE imgui-eacp eacp-mesh imgui_eacp_warnings)

set(BUNDLE_ID "com.imguieacp.model")

set_target_properties(Model PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "glTF Inspector"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(Model)
set_default_target_setting(Model)
