set(CMAKE_FOLDER Examples)

add_subdirectory(Demo)

# What a frame of the backend costs, against a load set by sliders rather than
# by whatever the demo window contains.
add_subdirectory(Bench)

# A glTF inspector: eacp's Mesh module draws the model, this backend's overlay
# reports it. The first consumer of everything the GPU plan's phases landed.
add_subdirectory(Model)

# The mixed example stacks an ImGui panel and a WebView in one window, so it
# only builds where eacp has a web engine to embed.
if (EACP_BUILD_WEBVIEW)
    add_subdirectory(MixedViews)
endif ()
