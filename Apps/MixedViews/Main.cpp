#include <imgui-eacp/ImGuiEacp.h>

#include <eacp/WebView/WebView.h>

#include <algorithm>
#include <string>

// An ImGui panel and a WebView side by side in one window, wired to each other.
//
// The point is that neither one is special. `Gui::ImGuiView` is a GPUView
// drawing through Metal / D3D12, `Graphics::WebView` hosts the platform web
// engine, and both are plain eacp Views the root lays out, sizes and hands
// mouse events to — a draggable splitter between them moves both at once.
//
// The wiring runs both ways: the sliders on the left push values into the page
// as JavaScript, and the button in the page posts a message back that the panel
// on the left counts.

using namespace eacp;

namespace
{
struct Settings
{
    float hue = 210.0f;
    float pulse = 1.0f;
    bool glow = true;
};

std::string pageHtml()
{
    return R"HTML(
<!doctype html><html><head><meta charset="utf-8"><style>
  :root { color-scheme: dark; --hue: 210; --pulse: 1; }
  html, body { margin:0; height:100%; overflow:hidden;
               font-family:-apple-system, system-ui, sans-serif; }
  body { display:flex; flex-direction:column; gap:18px; align-items:center;
         justify-content:center; user-select:none; color:#eaf2ff;
         background:radial-gradient(120% 120% at 30% 15%,
                    hsl(var(--hue) 55% 26%), hsl(var(--hue) 60% 8%)); }
  h1 { margin:0; font-size:18px; font-weight:600; letter-spacing:0.2px; }
  p  { margin:0; font-size:12px; color:#9fb6d6; max-width:32ch;
       text-align:center; line-height:1.5; }
  #orb { width:120px; height:120px; border-radius:50%;
         background:hsl(var(--hue) 70% 58%);
         animation:pulse calc(2s / var(--pulse)) ease-in-out infinite; }
  #orb.flat { animation:none; box-shadow:none; }
  @keyframes pulse {
    0%,100% { transform:scale(1);    box-shadow:0 0 24px 4px hsl(var(--hue) 70% 50% / 0.5); }
    50%     { transform:scale(1.08); box-shadow:0 0 52px 12px hsl(var(--hue) 70% 60% / 0.75); }
  }
  button { border:1px solid hsl(var(--hue) 45% 42%);
           background:hsl(var(--hue) 45% 22%); color:#eaf2ff;
           padding:9px 18px; border-radius:9px; font-size:13px; cursor:pointer; }
  button:hover { background:hsl(var(--hue) 45% 30%); }
</style></head><body>
  <h1>Live web content</h1>
  <div id="orb"></div>
  <p>Styled by the native ImGui sliders on the left. The button posts a message
     the other way, back into the C++ panel.</p>
  <button id="ping">Send a ping &rarr;</button>
  <script>
    window.setHue = function (hue) {
      document.documentElement.style.setProperty('--hue', hue);
    };
    window.setPulse = function (pulse) {
      document.documentElement.style.setProperty('--pulse', pulse);
    };
    window.setGlow = function (on) {
      document.getElementById('orb').classList.toggle('flat', !on);
    };
    document.getElementById('ping').addEventListener('click', function () {
      if (window.webkit && window.webkit.messageHandlers.ping)
        window.webkit.messageHandlers.ping.postMessage('1');
      else if (window.chrome && window.chrome.webview)
        window.chrome.webview.postMessage('ping');
    });
  </script>
</body></html>)HTML";
}

// The native half: sliders over the shared settings, plus whatever the page has
// sent back. Nothing here knows it is talking to a web engine — it edits a
// struct and reports which fields changed.
struct ControlPanel final : Gui::ImGuiView
{
    void draw() override
    {
        const auto bounds = getLocalBounds();

        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({bounds.w, bounds.h});

        constexpr auto flags = ImGuiWindowFlags_NoTitleBar
                               | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoCollapse
                               | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (!ImGui::Begin("Controls", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Native controls");
        ImGui::TextWrapped(
            "Metal / D3D12 through eacp's GPU module, in the same window "
            "as the WebView beside it.");

        ImGui::Spacing();

        // Only a real change is pushed across. An immediate-mode UI runs this
        // block every refresh, and evaluating a script sixty times a second to
        // set a value the page already has is the one way this gets expensive.
        auto changed = false;

        changed |= ImGui::SliderFloat("Hue", &settings.hue, 0.0f, 360.0f, "%.0f");
        changed |=
            ImGui::SliderFloat("Pulse", &settings.pulse, 0.25f, 4.0f, "%.2fx");
        changed |= ImGui::Checkbox("Glow", &settings.glow);

        if (changed)
            onSettingsChanged(settings);

        ImGui::SeparatorText("From the page");
        ImGui::Text("Pings received: %d", pings);

        if (pings == 0)
            ImGui::TextDisabled("Press the button on the right.");

        ImGui::Spacing();
        ImGui::SeparatorText("Surface");
        ImGui::Text("%.0f x %.0f pt at %.2f px/pt",
                    bounds.w,
                    bounds.h,
                    (double) backingScale());
        ImGui::Text("%.1f fps", (double) ImGui::GetIO().Framerate);

        ImGui::End();
    }

    std::function<void(const Settings&)> onSettingsChanged = [](const Settings&) {};

    Settings settings;
    int pings = 0;
};

// A plain View between the two panels. It exists to make the point that the
// ImGui surface is an ordinary sibling: dragging this resizes it the same way
// it resizes the web view, and neither knows the other is there.
struct Splitter final : Graphics::View
{
    Splitter()
    {
        setHandlesMouseEvents(true);
        setMouseCursor(Graphics::MouseCursor::ResizeLeftRight);
    }

    void paint(Graphics::Context& g) override
    {
        g.setColor(Graphics::Color::white(0.12f));
        g.fillRect(getLocalBounds());
    }

    void mouseDragged(const Graphics::MouseEvent&) override
    {
        auto* host = getParent();

        if (host != nullptr)
            onDragged(host->getMousePosition().x);
    }

    std::function<void(float)> onDragged = [](float) {};
};

struct MixedRoot final : Graphics::View
{
    MixedRoot()
    {
        addChildren({controls, splitter, web});

        controls.onSettingsChanged = [this](const Settings& settings)
        { pushToPage(settings); };

        splitter.onDragged = [this](float x)
        {
            const auto bounds = getLocalBounds();
            split = std::clamp(x, minimumPane, bounds.w - minimumPane);
            resized();
        };

        // Both engines deliver a posted message through the same handler, so
        // the page's two postMessage spellings arrive here either way.
        web.addScriptMessageHandler("ping",
                                    [this](const std::string&)
                                    {
                                        ++controls.pings;
                                        controls.repaint();
                                    });

        // The page starts at its CSS defaults, so the panel's current values
        // are pushed once the document is there to receive them.
        web.onNavigationFinished = [this](const std::string&)
        { pushToPage(controls.settings); };

        web.loadHTML(pageHtml(), "https://localhost/");
    }

    void pushToPage(const Settings& settings)
    {
        web.evaluateJavaScript(
            "window.setHue(" + std::to_string((int) settings.hue) + ");"
            + "window.setPulse(" + std::to_string(settings.pulse) + ");"
            + "window.setGlow(" + (settings.glow ? "true" : "false") + ");");
    }

    void resized() override
    {
        const auto bounds = getLocalBounds();

        if (bounds.w <= 0.0f || bounds.h <= 0.0f)
            return;

        split = std::clamp(split, minimumPane, bounds.w - minimumPane);

        controls.setBounds({0.0f, 0.0f, split, bounds.h});
        splitter.setBounds({split, 0.0f, splitterWidth, bounds.h});
        web.setBounds({split + splitterWidth,
                       0.0f,
                       bounds.w - split - splitterWidth,
                       bounds.h});
    }

    static constexpr float splitterWidth = 6.0f;
    static constexpr float minimumPane = 220.0f;

    ControlPanel controls;
    Splitter splitter;
    Graphics::WebView web;
    float split = 380.0f;
};

Graphics::WindowOptions windowOptions()
{
    auto options = Graphics::WindowOptions {};

    options.width = 1180;
    options.height = 720;
    options.minWidth = 720;
    options.minHeight = 420;
    options.title = "imgui-eacp — ImGui beside a WebView";
    options.backgroundColor = Graphics::Color {0.09f, 0.10f, 0.13f};

    return options;
}

struct MixedApp
{
    MixedApp() { window.setContentView(root); }

    MixedRoot root;
    Graphics::Window window {windowOptions()};
};
} // namespace

int main()
{
    return Apps::run<MixedApp>();
}
