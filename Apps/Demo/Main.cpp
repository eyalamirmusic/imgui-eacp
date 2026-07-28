#include <imgui-eacp/ImGuiEacp.h>

// Dear ImGui rendered by eacp's GPU module — Metal on Apple platforms, D3D12 on
// Windows, with nothing to pick. The panel is an ordinary eacp View: it is what
// the window's content view is set to, and everything it draws goes through the
// same swapchain any other GPUView would use.
//
// The demo window is the smoke test. It exercises every widget, every nested
// clip rect and the whole font atlas, so if it draws correctly the backend is
// correct — and the second window below reports what the view knows about the
// display it is on.

using namespace eacp;

namespace
{
struct DemoView final : Gui::ImGuiView
{
    void draw() override
    {
        ImGui::ShowDemoWindow();
        drawBackendPanel();
    }

    void drawBackendPanel()
    {
        ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({360.0f, 260.0f}, ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("eacp backend"))
        {
            ImGui::End();
            return;
        }

        const auto bounds = getLocalBounds();
        const auto& io = ImGui::GetIO();

        ImGui::SeparatorText("Surface");
        ImGui::Text("Renderer     %s", io.BackendRendererName);
        ImGui::Text("View size    %.0f x %.0f pt", bounds.w, bounds.h);
        ImGui::Text("Backing      %.2f px/pt", (double) backingScale());
        ImGui::Text("Samples      %d", sampleCount());

        ImGui::SeparatorText("Frame");
        ImGui::Text("%.1f fps  (%.2f ms)",
                    (double) io.Framerate,
                    1000.0 / (double) io.Framerate);

        // The previous frame's, since this one has not been assembled yet.
        const auto& drawn = getRenderer();
        ImGui::Text("%d vertices, %d indices, %d draws",
                    drawn.getVertexCount(),
                    drawn.getIndexCount(),
                    drawn.getDrawCount());

        // Per-frame geometry goes through GPU::StreamingBuffers, which recycles,
        // so this stops moving once the pools are warm however busy the UI gets.
        // A frame count above zero here is a GPU allocation in the frame loop.
        const auto created = GPU::Device::shared().buffersCreated();
        ImGui::Text("%d GPU buffers created (%+d this frame)",
                    created,
                    created - lastBufferCount);
        lastBufferCount = created;

        ImGui::SeparatorText("Clear colour");

        // The pass behind the UI is eacp's, not ImGui's, so this drives a
        // native render-pass descriptor rather than an ImGui style colour.
        if (ImGui::ColorEdit3("##clear", background))
            setClearColor({background[0], background[1], background[2]});

        ImGui::End();
    }

    float background[3] = {0.09f, 0.10f, 0.13f};
    int lastBufferCount = 0;
};

Graphics::WindowOptions windowOptions()
{
    auto options = Graphics::WindowOptions {};

    options.width = 1280;
    options.height = 800;
    options.minWidth = 640;
    options.minHeight = 420;
    options.title = "imgui-eacp — Dear ImGui on eacp's GPU module";
    options.backgroundColor = Graphics::Color {0.09f, 0.10f, 0.13f};

    return options;
}

struct DemoApp
{
    DemoApp() { window.setContentView(view); }

    DemoView view;
    Graphics::Window window {windowOptions()};
};
} // namespace

int main()
{
    return Apps::run<DemoApp>();
}
