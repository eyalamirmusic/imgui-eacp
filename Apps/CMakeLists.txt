set(CMAKE_FOLDER Examples)

add_subdirectory(Demo)

# What a frame of the backend costs, against a load set by sliders rather than
# by whatever the demo window contains.
add_subdirectory(Bench)

# The mixed example stacks an ImGui panel and a WebView in one window, so it
# only builds where eacp has a web engine to embed.
if (EACP_BUILD_WEBVIEW)
    add_subdirectory(MixedViews)
endif ()
