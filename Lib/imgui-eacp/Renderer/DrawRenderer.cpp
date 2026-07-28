#include "DrawRenderer.h"

#include <cstring>

namespace eacp::Gui
{
DrawShader::DrawShader()
{
    image.sampling = drawSampling;
    compile();
}

void DrawShader::define()
{
    auto position = vertexInput(&DrawVertex::position);
    auto uv = vertexInput(&DrawVertex::uv);
    auto color = vertexInput(&DrawVertex::color);

    auto local = position - displayOrigin;
    auto ndcX = local.x() / displaySize.x() * 2.0f - 1.0f;
    auto ndcY = 1.0f - local.y() / displaySize.y() * 2.0f;

    setPosition(float4(ndcX, ndcY, 0.0f, 1.0f));
    setFragment(sample(image, varying(uv)) * varying(color));
}

namespace
{
// By the shifts rather than copying the word straight through: the default
// packing already has the bytes in RGBA order, but IMGUI_USE_BGRA_PACKED_COLOR
// swaps two of them, and a reinterpret would draw that build in the wrong
// colours instead of failing.
std::uint8_t channel(ImU32 color, int shift)
{
    return (std::uint8_t) ((color >> shift) & 0xFF);
}

// Whatever ImGui was built with. The indices are copied through untouched, so
// there is nothing to widen them for — 16 bits by default.
constexpr auto drawIndexFormat =
    sizeof(ImDrawIdx) == 2 ? GPU::IndexFormat::UInt16 : GPU::IndexFormat::UInt32;

// Writes how long a scope took where the renderer reports it. A destructor
// rather than a pair of calls because encode() returns early on an empty frame,
// and a frame that costs nothing still has to be recorded as costing nothing —
// a stale reading is worse than no reading.
class ScopedTimer
{
public:
    explicit ScopedTimer(std::chrono::nanoseconds& targetToUse)
        : target(targetToUse)
    {
    }

    ~ScopedTimer() { target = std::chrono::steady_clock::now() - start; }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::chrono::nanoseconds& target;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};
} // namespace

DrawRenderer::DrawRenderer(int sampleCount)
{
    // AlphaBlend rather than the default None: every edge ImGui draws is
    // antialiased in the vertex colours, so an opaque pipeline would punch the
    // coverage straight through whatever is behind the UI.
    shader.prepare(sampleCount,
                   false,
                   GPU::PrimitiveTopology::Triangles,
                   GPU::BlendMode::AlphaBlend);
}

ImTextureID DrawRenderer::toTextureID(const GPU::Texture& texture)
{
    return (ImTextureID) (std::uintptr_t) &texture;
}

void DrawRenderer::createTexture(ImTextureData& texture)
{
    IM_ASSERT(texture.Format == ImTextureFormat_RGBA32
              && "imgui-eacp uploads RGBA32 atlases only");

    auto descriptor = GPU::TextureDescriptor {};
    descriptor.width = texture.Width;
    descriptor.height = texture.Height;
    descriptor.format = GPU::TextureFormat::RGBA8Unorm;

    auto& created =
        textures.createNew(GPU::Device::shared(), descriptor, texture.GetPixels());

    texture.SetTexID(toTextureID(created));
    texture.SetStatus(ImTextureStatus_OK);
    texture.BackendUserData = &created;
}

void DrawRenderer::uploadRegions(ImTextureData& texture)
{
    auto* target = static_cast<GPU::Texture*>(texture.BackendUserData);

    if (target == nullptr)
        return;

    // Region uploads rather than the whole atlas: ImGui adds glyphs one at a
    // time as they are first drawn, and re-sending megabytes to move a few
    // hundred bytes is what Texture::update's region overload exists to avoid.
    for (const auto& region: texture.Updates)
        target->update(
            {(float) region.x, (float) region.y, (float) region.w, (float) region.h},
            texture.GetPixelsAt(region.x, region.y),
            (std::size_t) texture.GetPitch());

    texture.SetStatus(ImTextureStatus_OK);
}

void DrawRenderer::destroyTexture(ImTextureData& texture)
{
    if (auto* target = static_cast<GPU::Texture*>(texture.BackendUserData))
        textures.removeItem(*target);

    texture.SetTexID(ImTextureID_Invalid);
    texture.SetStatus(ImTextureStatus_Destroyed);
    texture.BackendUserData = nullptr;
}

void DrawRenderer::updateTexture(ImTextureData& texture)
{
    if (texture.Status == ImTextureStatus_WantCreate)
        createTexture(texture);
    else if (texture.Status == ImTextureStatus_WantUpdates)
        uploadRegions(texture);

    // UnusedFrames guards the destroy: ImGui queues one as soon as an atlas
    // stops being drawn, but a frame already submitted may still sample it.
    if (texture.Status == ImTextureStatus_WantDestroy && texture.UnusedFrames > 0)
        destroyTexture(texture);
}

void DrawRenderer::releaseTextures(ImVector<ImTextureData*>& atlasTextures)
{
    for (auto* texture: atlasTextures)
        if (texture->RefCount == 1)
            destroyTexture(*texture);
}

void DrawRenderer::appendVertices(const ImDrawList& list)
{
    const auto first = vertices.size();
    const auto count = list.VtxBuffer.Size;

    vertices.resize(first + count);

    const auto* source = list.VtxBuffer.Data;
    auto* target = vertices.data() + first;

    for (auto index = 0; index < count; ++index)
    {
        const auto& from = source[index];
        auto& to = target[index];

        to.position[0] = from.pos.x;
        to.position[1] = from.pos.y;
        to.uv[0] = from.uv.x;
        to.uv[1] = from.uv.y;
        to.color = {{channel(from.col, IM_COL32_R_SHIFT),
                     channel(from.col, IM_COL32_G_SHIFT),
                     channel(from.col, IM_COL32_B_SHIFT),
                     channel(from.col, IM_COL32_A_SHIFT)}};
    }
}

// A straight copy. Every draw list still shares one vertex stream, but the
// offset into it rides on the draw's base vertex instead of being added into
// each index — so the values keep ImDrawIdx's width, and the whole frame's
// indices are one memcpy rather than a pass with an add per element.
void DrawRenderer::appendIndices(const ImDrawList& list, const ImDrawCmd& command)
{
    const auto first = indices.size();
    const auto count = (int) command.ElemCount;

    indices.resize(first + count);

    std::memcpy(indices.data() + first,
                list.IdxBuffer.Data + command.IdxOffset,
                sizeof(ImDrawIdx) * (std::size_t) count);
}

void DrawRenderer::appendList(const ImDrawList& list, ImVec2 origin, ImVec2 scale)
{
    const auto base = vertices.size();
    appendVertices(list);

    for (const auto& command: list.CmdBuffer)
    {
        if (command.UserCallback != nullptr)
        {
            // ResetRenderState asks the backend to restore what a callback
            // clobbered, which bindState already does after every callback.
            if (command.UserCallback != ImDrawCallback_ResetRenderState)
            {
                auto record = DrawCommand {};
                record.callbackList = &list;
                record.callbackCommand = &command;
                commands.add(record);
            }

            continue;
        }

        if (command.ElemCount == 0)
            continue;

        // Clip rects arrive in ImGui's space; the scissor rect wants render
        // target pixels, which is the same origin and y sense times the
        // framebuffer scale. RenderPass clamps it to the target for us.
        const auto minX = (command.ClipRect.x - origin.x) * scale.x;
        const auto minY = (command.ClipRect.y - origin.y) * scale.y;
        const auto maxX = (command.ClipRect.z - origin.x) * scale.x;
        const auto maxY = (command.ClipRect.w - origin.y) * scale.y;

        if (maxX <= minX || maxY <= minY)
            continue;

        auto record = DrawCommand {};
        record.scissor = {minX, minY, maxX - minX, maxY - minY};
        record.texture = (const GPU::Texture*) (std::uintptr_t) command.GetTexID();
        record.firstIndex = indices.size();
        record.indexCount = (int) command.ElemCount;
        record.baseVertex = base + (int) command.VtxOffset;

        appendIndices(list, command);
        commands.add(record);
    }
}

void DrawRenderer::uploadGeometry()
{
    vertexBuffer = nullptr;
    indexBuffer = nullptr;

    if (vertices.empty() || indices.empty())
        return;

    vertexBuffer = &vertexStream.write(
        vertices.data(), sizeof(DrawVertex) * (std::size_t) vertices.size());

    indexBuffer = &indexStream.write(
        indices.data(), sizeof(ImDrawIdx) * (std::size_t) indices.size());
}

void DrawRenderer::prepare(ImDrawData& drawData)
{
    auto timer = ScopedTimer {prepareTime};

    commands.clear();
    vertices.clear();
    indices.clear();

    // Textures first: a create or an upload has to land before the draw that
    // samples it, and both want no encoder open.
    if (drawData.Textures != nullptr)
        for (auto* texture: *drawData.Textures)
            if (texture->Status != ImTextureStatus_OK)
                updateTexture(*texture);

    shader.displayOrigin = Array {drawData.DisplayPos.x, drawData.DisplayPos.y};
    shader.displaySize = Array {drawData.DisplaySize.x, drawData.DisplaySize.y};

    for (const auto* list: drawData.CmdLists)
        appendList(*list, drawData.DisplayPos, drawData.FramebufferScale);

    uploadGeometry();
}

void DrawRenderer::bindState(GPU::RenderPass& pass)
{
    pass.setPipeline(shader.pipeline());
    pass.setVertexBuffer(*vertexBuffer);
    pass.setVertexUniforms(shader);
    pass.setFragmentUniforms(shader);
}

void DrawRenderer::encode(GPU::RenderPass& pass)
{
    auto timer = ScopedTimer {encodeTime};

    if (commands.empty() || vertexBuffer == nullptr || indexBuffer == nullptr)
        return;

    bindState(pass);

    for (const auto& command: commands)
    {
        if (command.callbackCommand != nullptr)
        {
            command.callbackCommand->UserCallback(command.callbackList,
                                                  command.callbackCommand);
            bindState(pass);
            continue;
        }

        if (command.texture == nullptr)
            continue;

        pass.setScissorRect(command.scissor);
        pass.setFragmentTexture(*command.texture, 0, drawSampling);
        pass.drawIndexed(*indexBuffer,
                         command.indexCount,
                         drawIndexFormat,
                         command.firstIndex,
                         command.baseVertex);
    }

    // The pass outlives this call — anything drawn over the UI would otherwise
    // inherit the last widget's clip.
    pass.clearScissorRect();
}
} // namespace eacp::Gui
