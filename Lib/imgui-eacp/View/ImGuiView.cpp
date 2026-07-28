#include "ImGuiView.h"

#include "../Input/KeyMap.h"

#include <eacp/Core/App/Clipboard.h>

#include <algorithm>
#include <cfloat>

namespace eacp::Gui
{
namespace
{
// Makes a view's context current for the length of a call and puts back
// whatever was current before. Every public entry point opens one, which is
// what lets two ImGuiViews coexist and what keeps this view from stealing the
// context from an unrelated one mid-frame.
class ContextScope
{
public:
    explicit ContextScope(ImGuiContext* context)
        : previous(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(context);
    }

    ~ContextScope() { ImGui::SetCurrentContext(previous); }

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

private:
    ImGuiContext* previous;
};

const char* readClipboard(ImGuiContext*)
{
    auto* view = static_cast<ImGuiView*>(ImGui::GetIO().BackendPlatformUserData);

    if (view == nullptr)
        return nullptr;

    return view->pasteFromClipboard();
}

void writeClipboard(ImGuiContext*, const char* text)
{
    if (text != nullptr)
        Clipboard::copyText(text);
}

// A wheel unit scrolls an ImGui window by five lines of text, so a trackpad's
// delta — which eacp reports in points — is divided by that to make the content
// follow the fingers 1:1. A notched wheel already reports one line per detent,
// which is the unit ImGui's other backends feed it.
float toWheelUnits(float delta, bool precise, float fontSize)
{
    if (!precise)
        return delta;

    return delta / std::max(1.0f, fontSize * 5.0f);
}

bool isPrintable(char character)
{
    const auto value = static_cast<unsigned char>(character);
    return value >= 0x20 && value != 0x7f;
}

// The system UI font, as a path rather than through the platform's font APIs:
// ImGui rasterizes with stb_truetype and wants a file, and every desktop this
// runs on ships one at a fixed location.
std::string platformFontPath()
{
#if defined(__APPLE__)
    return "/System/Library/Fonts/SFNS.ttf";
#elif defined(_WIN32)
    return "C:\\Windows\\Fonts\\segoeui.ttf";
#else
    return {};
#endif
}
} // namespace

ImGuiView::ImGuiView(const ViewOptions& optionsToUse)
    : options(optionsToUse)
    , renderer(optionsToUse.sampleCount)
{
    setSampleCount(options.sampleCount);
    setContinuous(options.continuous);
    setHandlesMouseEvents(true);
    setGrabsFocusOnMouseDown(true);

    auto* previous = ImGui::GetCurrentContext();

    context = ImGui::CreateContext();
    configureContext();

    ImGui::SetCurrentContext(previous);
}

ImGuiView::~ImGuiView()
{
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context);

    // The one moment ImGui cannot ask for the destroys itself: the atlases go
    // down with the context, so the GPU textures have to be handed back first.
    renderer.releaseTextures(ImGui::GetPlatformIO().Textures);

    // ImGui::Shutdown asserts that a backend cleared what it registered, which
    // is how it catches a context outliving the thing that was drawing it.
    auto& io = ImGui::GetIO();
    io.BackendPlatformName = nullptr;
    io.BackendRendererName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendRendererUserData = nullptr;

    ImGui::DestroyContext(context);

    ImGui::SetCurrentContext(previous == context ? nullptr : previous);
}

void ImGuiView::configureContext()
{
    auto& io = ImGui::GetIO();

    io.BackendPlatformName = "eacp";
    io.BackendRendererName = "imgui-eacp";
    io.BackendPlatformUserData = this;
    io.IniFilename = options.iniFilename;

    // HasTextures is what lets ImGui grow the atlas a glyph at a time instead
    // of rebuilding it, and HasVtxOffset lets it keep a window's geometry in
    // one draw list past 64k vertices — the renderer hands every draw a base
    // vertex, so both are true here.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    if (options.darkStyle)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsLight();

    ImGui::GetStyle().FontSizeBase = options.fontSize;
    loadFont();

    auto& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = readClipboard;
    platformIo.Platform_SetClipboardTextFn = writeClipboard;
}

void ImGuiView::loadFont() const
{
    const auto path =
        options.fontPath.empty() ? platformFontPath() : options.fontPath;

    if (path.empty())
        return;

    auto config = ImFontConfig {};

    // A missing system font is not a programming error — the path is an
    // assumption about the OS install — so ask for a null return rather than
    // the assert, and let the built-in font stand in.
    config.Flags |= ImFontFlags_NoLoadError;

    // Size 0 leaves the font dynamically sized, so it follows FontSizeBase and
    // whatever PushFont asks for rather than being baked to one size here.
    ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &config);
}

const char* ImGuiView::pasteFromClipboard()
{
    clipboardText = Clipboard::getText();
    return clipboardText.c_str();
}

bool ImGuiView::wantsMouse() const
{
    auto scope = ContextScope {context};
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiView::wantsKeyboard() const
{
    auto scope = ContextScope {context};
    return ImGui::GetIO().WantCaptureKeyboard;
}

double ImGuiView::secondsSinceLastFrame()
{
    const auto now = std::chrono::steady_clock::now();

    if (!hasFrameTime)
    {
        hasFrameTime = true;
        lastFrame = now;
        return 1.0 / 60.0;
    }

    const auto elapsed = std::chrono::duration<double> {now - lastFrame}.count();
    lastFrame = now;

    // ImGui rejects a non-positive delta, and a stall long enough to matter
    // should step the animations rather than jump them.
    return std::clamp(elapsed, 1.0 / 1000.0, 0.1);
}

void ImGuiView::beginFrame()
{
    auto& io = ImGui::GetIO();

    const auto bounds = getLocalBounds();
    const auto scale = backingScale();

    // Logical points, exactly like the rest of the view hierarchy. The atlas is
    // rasterized at `scale` on top of that, so a 15pt font is 15pt on both
    // panels of a mixed-DPI setup.
    io.DisplaySize = ImVec2 {bounds.w, bounds.h};
    io.DisplayFramebufferScale = ImVec2 {scale, scale};
    io.DeltaTime = (float) secondsSinceLastFrame();

    updateFocus();

    ImGui::NewFrame();
}

void ImGuiView::updateFocus()
{
    const auto focused = hasFocus();

    if (focused == wasFocused)
        return;

    wasFocused = focused;

    // Losing focus has to release whatever was held, or a key still down when
    // the window went away stays down forever.
    ImGui::GetIO().AddFocusEvent(focused);
}

void ImGuiView::updateCursor()
{
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0)
        return;

    setMouseCursor(toMouseCursor(ImGui::GetMouseCursor()));
}

void ImGuiView::render(GPU::Frame& frame)
{
    auto scope = ContextScope {context};

    beginFrame();
    draw();
    ImGui::Render();

    auto* drawData = ImGui::GetDrawData();

    if (drawData == nullptr)
        return;

    // Everything that touches a resource happens before the pass opens: both
    // backends want their uploads outside an encoder.
    renderer.prepare(*drawData);
    updateCursor();

    auto pass = frame.beginPass({.clearColor = options.clearColor,
                                 .clear = true,
                                 .label = options.passLabel});
    renderer.encode(pass);
}

void ImGuiView::sendMousePosition(const Graphics::MouseEvent& event)
{
    ImGui::GetIO().AddMousePosEvent(event.pos.x, event.pos.y);
}

void ImGuiView::mouseDown(const Graphics::MouseEvent& event)
{
    auto scope = ContextScope {context};

    sendMousePosition(event);
    ImGui::GetIO().AddMouseButtonEvent(toImGuiButton(event.button), true);

    repaint();
}

void ImGuiView::mouseUp(const Graphics::MouseEvent& event)
{
    auto scope = ContextScope {context};

    sendMousePosition(event);
    ImGui::GetIO().AddMouseButtonEvent(toImGuiButton(event.button), false);

    repaint();
}

void ImGuiView::mouseDragged(const Graphics::MouseEvent& event)
{
    auto scope = ContextScope {context};

    sendMousePosition(event);
    repaint();
}

void ImGuiView::mouseMoved(const Graphics::MouseEvent& event)
{
    auto scope = ContextScope {context};

    sendMousePosition(event);
    repaint();
}

void ImGuiView::mouseExited(const Graphics::MouseEvent&)
{
    auto scope = ContextScope {context};

    // The pointer is nowhere, which is what ImGui reads a coordinate off the
    // end of the world as — anything else leaves a widget hovered forever.
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    repaint();
}

void ImGuiView::mouseWheel(const Graphics::MouseEvent& event)
{
    auto scope = ContextScope {context};
    auto& io = ImGui::GetIO();

    const auto fontSize = ImGui::GetStyle().FontSizeBase;

    sendMousePosition(event);
    io.AddMouseWheelEvent(
        toWheelUnits(event.delta.x, event.preciseScrolling, fontSize),
        toWheelUnits(event.delta.y, event.preciseScrolling, fontSize));

    repaint();
}

void ImGuiView::sendText(const Graphics::KeyEvent& event)
{
    // Return, Tab and Escape arrive with a control character in `characters`;
    // ImGui wants those as keys only, never as typed text.
    for (auto character: event.characters)
        if (!isPrintable(character))
            return;

    ImGui::GetIO().AddInputCharactersUTF8(event.characters.c_str());
}

void ImGuiView::keyDown(const Graphics::KeyEvent& event)
{
    auto scope = ContextScope {context};
    auto& io = ImGui::GetIO();

    addModifiers(io, event.modifiers);
    io.AddKeyEvent(toImGuiKey(event.keyCode), true);

    // A shortcut is not text: Cmd+V has to reach the paste handler rather than
    // inserting a "v" into whatever field has focus.
    if (!event.modifiers.command && !event.modifiers.control)
        sendText(event);

    repaint();
}

void ImGuiView::keyUp(const Graphics::KeyEvent& event)
{
    auto scope = ContextScope {context};
    auto& io = ImGui::GetIO();

    addModifiers(io, event.modifiers);
    io.AddKeyEvent(toImGuiKey(event.keyCode), false);

    repaint();
}
} // namespace eacp::Gui
