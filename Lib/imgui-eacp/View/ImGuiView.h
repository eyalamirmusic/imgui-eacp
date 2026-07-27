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

private:
    void configureContext();
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
