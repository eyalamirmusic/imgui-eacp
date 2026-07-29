#include "Camera.h"
#include "Inspector.h"
#include "SampleModel.h"
#include "Viewport.h"

#include <imgui-eacp/ImGuiEacp.h>

// A glTF inspector: eacp's Mesh module draws the model, and this backend's ImGui
// overlay reports what it loaded and what it cost.
//
// The split is the point. Loading a glTF and drawing a mesh are in eacp, where
// Sprites and Text already are and where anything else can use them; the panels
// are here, where the ImGui dependency is. Nothing in Lib/ knows this app
// exists.
//
// It is also the first consumer of every gap the GPU plan closed: a base vertex
// per primitive, packed vertex formats, back-face culling with a front-face
// convention, separate depth compare and depth write, mipmapped textures, and
// the per-pass GPU timings the Frame panel reports.

using namespace eacp;

namespace
{
// The model draws into a texture rather than the drawable, so its pass gets a
// depth buffer the UI's pass does not have. A texture target is single-sampled
// and this one is RGBA rather than the drawable's BGRA — the renderer is built
// to match it, which is what the two arguments here are.
constexpr auto viewportSamples = 1;

struct ModelView final : Gui::ImGuiView
{
    ModelView()
        : Gui::ImGuiView({.passLabel = "ui"})
        , renderer(viewportSamples,
                   GPU::pixelFormatFor(GPU::TextureFormat::RGBA8Unorm))
    {
        loadSample();
    }

    void loadSample()
    {
        auto document = ModelApp::sampleGltfDocument();
        apply(Mesh::loadGltfFromMemory(document.data(), document.size()),
              "built-in sample");
    }

    void loadFile(const std::string& path) { apply(Mesh::loadGltf(path), path); }

    void apply(Mesh::LoadResult result, const std::string& describedAs)
    {
        if (!result)
        {
            // The previous model stays on screen. A failed open that also blanks
            // the viewport loses the thing you were looking at in order to tell
            // you about the thing you did not get.
            loadError = describedAs + ": " + result.error;
            return;
        }

        loadError.clear();
        source = describedAs;
        data = std::move(result.data);

        renderer.setModel(data);
        camera.frame(data);

        inspector.selectedNode = -1;
    }

    void draw() override
    {
        drawViewportWindow();
        drawInspectorWindow();
    }

    // Runs after ImGui has been assembled and before its pass opens, which is
    // where a pass of our own belongs — see ImGuiView::onBeforePass.
    void beforePass(GPU::Frame& frame) override
    {
        if (!viewport.isValid())
            return;

        auto options = Mesh::RenderOptions {};

        options.view = camera.view();
        options.projection = camera.projection(viewport.aspect());
        options.shading = shading;
        options.lightDirection = camera.lightDirection();

        viewport.render(frame, renderer, options);
    }

    void drawViewportWindow()
    {
        ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({720.0f, 560.0f}, ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Viewport"))
        {
            ImGui::End();
            return;
        }

        drawViewportControls();

        auto available = ImGui::GetContentRegionAvail();

        // The texture is sized in pixels and the panel is measured in points, so
        // a Retina panel needs the backing scale applied or the model is
        // rendered at half resolution and stretched.
        auto scale = backingScale();
        auto pixelWidth = (int) (available.x * scale);
        auto pixelHeight = (int) (available.y * scale);

        if (viewport.resize(pixelWidth, pixelHeight))
        {
            ImGui::Image(Gui::DrawRenderer::toTextureID(viewport.texture()),
                         available);

            handleViewportInput();
        }
        else
        {
            ImGui::TextDisabled("no render target");
        }

        ImGui::End();
    }

    void drawViewportControls()
    {
        ImGui::SetNextItemWidth(220.0f);

        const char* modes[] = {"Unlit", "Lambert", "Normals"};
        auto current = (int) shading;

        if (ImGui::Combo("Shading", &current, modes, IM_ARRAYSIZE(modes)))
            shading = (Mesh::ShadingMode) current;

        ImGui::SameLine();

        if (ImGui::Button("Reframe"))
            camera.frame(data);

        ImGui::SameLine();
        ImGui::TextDisabled("drag to orbit, scroll to zoom, right-drag to pan");
    }

    // ImGui already knows whether the pointer is over the image and whether
    // anything else claimed the drag, so the camera is driven from its state
    // rather than from the view's own mouse events - which would fire over the
    // panels too.
    void handleViewportInput()
    {
        auto hovered = ImGui::IsItemHovered();
        auto& io = ImGui::GetIO();

        if (hovered && io.MouseWheel != 0.0f)
            camera.zoom(io.MouseWheel > 0.0f ? 0.88f : 1.0f / 0.88f);

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            auto drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

            camera.orbit(-drag.x * 0.01f, drag.y * 0.01f);
        }

        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            auto drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);

            camera.pan(drag.x, drag.y);
        }
    }

    void drawInspectorWindow()
    {
        ImGui::SetNextWindowPos({760.0f, 20.0f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({380.0f, 560.0f}, ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }

        drawSourceControls();

        ModelApp::drawFrameCost(
            renderer.lastStats(), loadError, renderer.modelTextures().size());

        ModelApp::drawModelSummary(data);
        ModelApp::drawNodeTree(data, inspector);
        ModelApp::drawMaterials(data);

        ImGui::End();
    }

    void drawSourceControls()
    {
        ImGui::TextDisabled("%s", source.c_str());

        // A path field rather than a file dialog because eacp has no file
        // chooser to call, and one typed path is a smaller thing to add than a
        // platform dialog to the framework. Both .gltf and .glb load through the
        // same call.
        ImGui::SetNextItemWidth(-70.0f);

        auto submitted =
            ImGui::InputTextWithHint("##path",
                                     "path to a .gltf or .glb",
                                     pathField,
                                     IM_ARRAYSIZE(pathField),
                                     ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();

        if ((ImGui::Button("Open") || submitted) && pathField[0] != '\0')
            loadFile(pathField);

        ImGui::SameLine();

        if (ImGui::Button("Sample"))
            loadSample();
    }

    Mesh::MeshData data;
    Mesh::MeshRenderer renderer;
    Mesh::ShadingMode shading = Mesh::ShadingMode::Lambert;

    ModelApp::OrbitCamera camera;
    ModelApp::Viewport viewport;
    ModelApp::InspectorState inspector;

    std::string source = "built-in sample";
    std::string loadError;

    char pathField[512] {};
};

struct ModelApplication
{
    ModelApplication()
    {
        window.setTitle("glTF inspector");
        window.setContentView(view);
    }

    ModelView view;
    Graphics::Window window;
};
} // namespace

int main()
{
    return Apps::run<ModelApplication>();
}
