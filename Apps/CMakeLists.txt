set(CMAKE_FOLDER Examples)

add_subdirectory(Demo)

# The mixed example stacks an ImGui panel and a WebView in one window, so it
# only builds where eacp has a web engine to embed.
if (EACP_BUILD_WEBVIEW)
    add_subdirectory(MixedViews)
endif ()
