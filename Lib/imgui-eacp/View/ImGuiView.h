#pragma once

#include "../Renderer/DrawRenderer.h"

#include <chrono>
#include <functional>
#include <string>

namespace eacp::Gui
{
struct ViewOptions
{
    Graphics::Color clearColor {0.09f, 0.10f, 0.13f};

    // Dear ImGui's own dark theme. Set false for the light one.
    bool darkStyle = true;

    // Base font size in logical points. The atlas is rasterized at the
    // display's density on top of this, so the same number is the same
    // apparent size on a Retina panel and a conventional one.
    float fontSize = 15.0f;

    // A TrueType/OpenType file loaded at `fontSize`. Empty means the platform's
    // own UI font — SF Pro on macOS, Segoe UI on Windows — and a path that
    // cannot be read falls back to ImGui's built-in font.
    //
    // Worth saying why this is not simply left alone. ImGui's built-in default
    // is ProggyClean, a bitmap-style font drawn for exactly 13px with no
    // antialiasing: crisp at that one size, blocky and soft at every other. At
    // any other size, and on a Retina panel especially, it reads as a
    // low-resolution app however right the rest of the pipeline is — so the
    // default here is a real vector font instead.
    std::string fontPath;

    // Redraw every display refresh rather than only when something asked for
    // it. On by default because an immediate-mode UI animates constantly —
    // caret blink, hover fades, anything driven by ImGui::GetTime() — and none
    // of that produces an event for the view to repaint from.
    bool continuous = true;

    // Multisampling is wasted here: every edge ImGui draws is either axis
    // aligned or already antialiased in the vertex colours, so MSAA costs
    // bandwidth to smooth nothing.
    int sampleCount = 1;

    // Where ImGui persists window positions. Null keeps it in memory only,
    // which is the right default for an app bundle whose working directory is
    // not somewhere it should be writing.
    const char* iniFilename = nullptr;

    // Names this view's render pass for eacp's GPU timer, which is also what
    // asks for it to be timed: the pass then turns up in
    // GPU::Device::lastFrameTimings() with how long the hardware spent on it.
    //
    // Empty by default, because timing is not free — it is two hardware counter
    // samples per pass — and an app that never looks at the numbers should not
    // pay for them. A window holding two ImGuiViews gives them different names
    // here, or the breakdown has two rows that cannot be told apart.
    std::string passLabel;
};

// A Dear ImGui surface that is an ordinary eacp View.
//
// It draws through eacp's GPU module, so the backend is Metal on Apple
// platforms and D3D12 on Windows with nothing to select, and it sits in the
// normal view hierarchy — give it bounds, put it beside a WebView, stack a
// shape layer over it, snapshot it with renderToImage.
//
// Each view owns its own ImGuiContext, so two of them in one window keep
// separate windows, settings and input state. Every entry point makes that
// context current for the duration and restores whatever was current before,
// which is what lets the second view work at all.
class ImGuiView : public GPU::GPUView
{
public:
    explicit ImGuiView(const ViewOptions& optionsToUse = {});
    ~ImGuiView() override;

    // The UI itself, called between NewFrame and Render with this view's
    // context current — so any ImGui:: call is legal inside it and nowhere
    // else. Override draw() instead when the panel has state of its own.
    std::function<void()> onDraw = [] {};

    // Run your own render passes on this frame, before the one the UI is drawn
    // in. What it is for is content that has to be rendered into a texture the
    // UI then shows — a 3D viewport, a preview thumbnail, anything with its own
    // pipeline state.
    //
    // Why a hook rather than letting an app override render(): the UI's pass
    // has no depth attachment, and both backends reject a draw whose pipeline
    // disagrees with the pass about that. So a 3D scene cannot simply be drawn
    // underneath the UI in the same pass — it needs a pass of its own, on a
    // target that has depth. Overriding render() to arrange that would fork
    // this view's frame logic into every app that wants a viewport, which is
    // the same reason DrawRenderer times itself rather than letting `Bench`
    // wrap the calls.
    //
    // It runs after the UI's own uploads and before its pass opens, so a
    // texture drawn here is written by the GPU before the pass that samples it
    // — the UI shows this frame's contents, not the previous frame's.
    std::function<void(GPU::Frame&)> onBeforePass = [](GPU::Frame&) {};

    // Whether ImGui consumed the last input it was given. An app routing
    // events to something else behind the UI asks these first.
    bool wantsMouse() const;
    bool wantsKeyboard() const;

    // What the pass clears to before the UI is drawn over it. Settable so a
    // panel can be recoloured from inside its own draw().
    void setClearColor(const Graphics::Color& color) { options.clearColor = color; }

    ImGuiContext* getContext() const { return context; }

    const DrawRenderer& getRenderer() const { return renderer; }

    // Reads the system clipboard into storage owned by this view and returns
    // it. Public because ImGui's paste hook is a plain function pointer that
    // has to reach back in through the context to call it.
    const char* pasteFromClipboard();

    void render(GPU::Frame& frame) override;

    void mouseDown(const Graphics::MouseEvent& event) override;
    void mouseUp(const Graphics::MouseEvent& event) override;
    void mouseDragged(const Graphics::MouseEvent& event) override;
    void mouseMoved(const Graphics::MouseEvent& event) override;
    void mouseExited(const Graphics::MouseEvent& event) override;
    void mouseWheel(const Graphics::MouseEvent& event) override;

    void keyDown(const Graphics::KeyEvent& event) override;
    void keyUp(const Graphics::KeyEvent& event) override;

protected:
    virtual void draw() { onDraw(); }
    virtual void beforePass(GPU::Frame& frame) { onBeforePass(frame); }

private:
    void configureContext();
    void loadFont() const;
    void beginFrame();
    void updateCursor();
    void updateFocus();

    void sendMousePosition(const Graphics::MouseEvent& event);
    void sendText(const Graphics::KeyEvent& event);

    double secondsSinceLastFrame();

    ViewOptions options;
    DrawRenderer renderer;

    ImGuiContext* context = nullptr;

    // ImGui reads the pasted text through a pointer it does not own, so the
    // string has to outlive the call that returned it.
    std::string clipboardText;

    std::chrono::steady_clock::time_point lastFrame;
    bool hasFrameTime = false;
    bool wasFocused = false;
};
} // namespace eacp::Gui
