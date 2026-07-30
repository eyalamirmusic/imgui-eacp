#include "Inspector.h"

#include <imgui.h>

namespace ModelApp
{
using namespace eacp::Mesh;

namespace
{
const char* alphaModeName(AlphaMode mode)
{
    switch (mode)
    {
        case AlphaMode::Opaque:
            return "opaque";
        case AlphaMode::Mask:
            return "mask";
        case AlphaMode::Blend:
            return "blend";
    }

    return "?";
}

void drawNode(const MeshData& data, int index, InspectorState& state)
{
    const auto& node = data.nodes[index];

    auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
                 | ImGuiTreeNodeFlags_DefaultOpen;

    if (node.children.size() == 0)
        flags |= ImGuiTreeNodeFlags_Leaf;

    if (index == state.selectedNode)
        flags |= ImGuiTreeNodeFlags_Selected;

    // The pointer form of the id, so two nodes sharing a name in the file stay
    // separate rows here rather than collapsing into one.
    auto open = ImGui::TreeNodeEx(
        (void*) (std::intptr_t) index, flags, "%s", node.name.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        state.selectedNode = index;

    if (node.mesh >= 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", data.meshes[node.mesh].name.c_str());
    }

    if (open)
    {
        for (auto child: node.children)
            drawNode(data, child, state);

        ImGui::TreePop();
    }
}

// The four numbers a node's transform is actually read for. The full sixteen
// says nothing at a glance, and this is a panel for finding a model's parts, not
// for auditing its matrices.
void drawSelectedNode(const MeshData& data, int index)
{
    const auto& node = data.nodes[index];
    const auto& world = node.worldTransform;

    auto origin = world.transformPoint({0.0f, 0.0f, 0.0f});

    ImGui::SeparatorText(node.name.c_str());
    ImGui::Text("World origin  %.3f, %.3f, %.3f",
                (double) origin.x,
                (double) origin.y,
                (double) origin.z);

    auto determinant = world.linearDeterminant();
    ImGui::Text("Determinant   %.4f", (double) determinant);

    // The one thing about a transform that changes how it is drawn rather than
    // only where.
    if (determinant < 0.0f)
        ImGui::TextDisabled("mirrored - drawn with a flipped front face");

    if (node.mesh < 0)
    {
        ImGui::TextDisabled("no mesh - positions its children only");
        return;
    }

    const auto& mesh = data.meshes[node.mesh];
    ImGui::Text("Primitives    %d", mesh.primitives.size());

    for (auto primitiveIndex: mesh.primitives)
    {
        const auto& primitive = data.primitives[primitiveIndex];

        auto materialName = primitive.material >= 0
                                ? data.materials[primitive.material].name.c_str()
                                : "(glTF default)";

        ImGui::BulletText("%d tris, base vertex %d, %s",
                          primitive.indexCount / 3,
                          primitive.baseVertex,
                          materialName);
    }
}
} // namespace

void drawNodeTree(const MeshData& data, InspectorState& state)
{
    if (!ImGui::CollapsingHeader("Nodes", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    for (auto i = 0; i < data.nodes.size(); ++i)
        if (data.nodes[i].parent < 0)
            drawNode(data, i, state);

    if (state.selectedNode >= 0 && state.selectedNode < data.nodes.size())
        drawSelectedNode(data, state.selectedNode);
}

void drawModelSummary(const MeshData& data)
{
    if (!ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::Text(
        "Nodes        %d  (%d drawn)", data.nodes.size(), data.drawOrder.size());
    ImGui::Text("Meshes       %d", data.meshes.size());
    ImGui::Text("Primitives   %d", data.primitives.size());
    ImGui::Text("Vertices     %d", data.vertices.size());
    ImGui::Text("Indices      %d", data.indices.size());

    // The claim §1.1 and §1.3 of the GPU plan are about, reported rather than
    // asserted: what the geometry costs at the packed vertex and the narrow
    // index, against what it would cost unpacked.
    auto narrow = fitsNarrowIndices(data);
    auto indexBytes = data.indices.size() * (narrow ? 2 : 4);
    auto vertexBytes = data.vertices.size() * (int) sizeof(MeshVertex);

    ImGui::Text("Index width  %d-bit%s",
                narrow ? 16 : 32,
                narrow ? "" : "  (a primitive passes 65536 vertices)");

    ImGui::Text("Geometry     %.1f KB",
                (double) (vertexBytes + indexBytes) / 1024.0);

    // 48 bytes is the same vertex with nothing packed: three floats of position,
    // three of normal, two of uv and four of colour. The normal is three rather
    // than the four MeshVertex packs, because the fourth is padding the packed
    // format needs and an unpacked one would not carry.
    auto unpacked = data.vertices.size() * 48 + data.indices.size() * 4;
    ImGui::TextDisabled("unpacked it would be %.1f KB (%.2fx)",
                        (double) unpacked / 1024.0,
                        (double) unpacked / (double) (vertexBytes + indexBytes));

    auto size = data.boundsMax - data.boundsMin;
    ImGui::Text("Bounds       %.2f x %.2f x %.2f",
                (double) size.x,
                (double) size.y,
                (double) size.z);
}

void drawMaterials(const MeshData& data)
{
    if (!ImGui::CollapsingHeader("Materials"))
        return;

    if (data.materials.size() == 0)
    {
        ImGui::TextDisabled("none - every primitive uses glTF's default");
        return;
    }

    for (auto i = 0; i < data.materials.size(); ++i)
    {
        const auto& material = data.materials[i];

        ImGui::PushID(i);

        auto color = ImVec4 {material.baseColor[0],
                             material.baseColor[1],
                             material.baseColor[2],
                             material.baseColor[3]};

        ImGui::ColorButton("##swatch", color, ImGuiColorEditFlags_AlphaPreview);
        ImGui::SameLine();

        if (ImGui::TreeNode("##material", "%s", material.name.c_str()))
        {
            ImGui::Text("Alpha       %s", alphaModeName(material.alphaMode));

            if (material.alphaMode == AlphaMode::Mask)
                ImGui::Text("Cutoff      %.3f", (double) material.alphaCutoff);

            ImGui::Text("Sides       %s",
                        material.doubleSided ? "double" : "single (back culled)");

            if (material.baseColorImage >= 0)
                ImGui::Text("Texture     image %d", material.baseColorImage);
            else
                ImGui::TextDisabled("no base colour texture");

            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

void drawFrameCost(const RenderStats& stats,
                   const std::string& loadError,
                   int textureCount)
{
    if (!ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!loadError.empty())
    {
        ImGui::TextColored({0.95f, 0.45f, 0.4f, 1.0f}, "%s", loadError.c_str());
        ImGui::Separator();
    }

    const auto& io = ImGui::GetIO();
    ImGui::Text("%.1f fps  (%.2f ms)",
                (double) io.Framerate,
                1000.0 / (double) io.Framerate);

    // The previous frame's. The panel is assembled before the pass that draws
    // the model runs, so this frame's numbers do not exist yet - the same
    // reason ImGui::GetDrawData() is a frame behind for anything reporting its
    // own geometry. They read zero on the very first frame and are current
    // after that.
    ImGui::Text("Draws        %d", stats.drawCalls);
    ImGui::Text("Triangles    %d", stats.triangles);

    // One switch per distinct pipeline state is the floor, and the number worth
    // watching: it climbing with the primitive count means the sort stopped
    // grouping them.
    ImGui::Text("Pipelines    %d bind%s",
                stats.pipelineSwitches,
                stats.pipelineSwitches == 1 ? "" : "s");

    ImGui::Text("Textures     %d", textureCount);

    ImGui::SeparatorText("GPU");

    // The passes eacp's timer was asked to measure. Both this app's passes are
    // labelled, so this is the whole frame split between the model and the UI
    // over it - which is the question an inspector's own overhead raises.
    const auto& timings = GPU::Device::shared().lastFrameTimings();

    if (timings.passes.size() == 0)
    {
        ImGui::TextDisabled("no counters on this device");
        return;
    }

    for (const auto& pass: timings.passes)
        ImGui::Text("%-10s   %.3f ms", pass.label.c_str(), pass.milliseconds);

    ImGui::Text("%-10s   %.3f ms", "frame", timings.milliseconds);
}
} // namespace ModelApp
