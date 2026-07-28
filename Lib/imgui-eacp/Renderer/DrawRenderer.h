#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/GPU/GPU.h>

#include <imgui.h>

#include <cstdint>
#include <optional>

namespace eacp::Gui
{
// The vertex the pipeline consumes. ImDrawVert packs its colour into a single
// RGBA8 word and eacp's VertexFormat has no normalized-byte attribute, so the
// colour is unpacked into floats — inside the copy from ImDrawVert that has to
// happen anyway, so it costs no extra pass over the data, only the width.
struct DrawVertex
{
    float position[2];
    float uv[2];
    float color[4];
};

// How every ImGui texture is sampled. Sampling belongs to the shader rather
// than the texture on this backend (see GPU::TextureSampling), so the value the
// shader compiles with and the value each bind passes have to be this one.
//
// Linear because the atlas is rasterized at the display's own density and drawn
// 1:1: filtering only shows where a glyph lands off a texel centre, which is
// exactly where nearest would alias.
constexpr GPU::TextureSampling drawSampling {.filter = GPU::TextureFilter::Linear,
                                             .addressMode =
                                                 GPU::TextureAddressMode::Clamp};

// ImGui's shader written once in the eacp EDSL, so the same definition emits
// MSL and HLSL and the Metal and D3D12 backends cannot drift apart.
//
// Vertices arrive in ImGui's own space — logical points, y down, origin at
// ImDrawData::DisplayPos — and the fragment is the texture sample times the
// vertex tint, which is the whole of ImGui's shading model.
struct DrawShader final : GPU::ShaderProgram
{
    DrawShader();

    void define() override;

    GPU::Uniform<GPU::Float2> displayOrigin;
    GPU::Uniform<GPU::Float2> displaySize;
    GPU::Uniform<GPU::Texture2D> image;

    EACP_SHADER(displayOrigin, displaySize, image)
};

// Turns one frame of ImDrawData into eacp GPU calls.
//
// Deliberately split in two. Creating a texture, uploading to one and rewriting
// a vertex buffer all want to happen with no encoder open, and a RenderPass is
// an open encoder for the whole of its lifetime — so prepare() does everything
// that touches a resource and encode() only records draws.
class DrawRenderer
{
public:
    // sampleCount must match the render target the draws land in
    // (GPUView::sampleCount()), since the pipeline is built here.
    explicit DrawRenderer(int sampleCount);

    // Textures and geometry. Call before Frame::beginPass.
    void prepare(ImDrawData& drawData);

    // The draws, in the order ImGui listed them. Call inside the pass.
    void encode(GPU::RenderPass& pass);

    // Hands back every texture ImGui still owns a reference to. The view calls
    // this before destroying its context, which is the one moment ImGui cannot
    // ask for the destroys itself.
    void releaseTextures(ImVector<ImTextureData*>& atlasTextures);

    // The ImTextureID this backend stores in ImDrawCmd, and therefore what
    // ImGui::Image() takes: a pointer to a GPU::Texture the caller keeps alive
    // for as long as the frame that draws it.
    static ImTextureID toTextureID(const GPU::Texture& texture);

    // What the last prepare() built. ImGui::GetDrawData() is null until Render()
    // has run, so a panel drawing itself cannot report its own geometry — these
    // are the previous frame's, which is the only answer there is mid-frame.
    int getVertexCount() const { return vertices.size(); }
    int getIndexCount() const { return indices.size(); }
    int getDrawCount() const { return commands.size(); }

private:
    // One set of buffers per frame that may be in flight. The geometry is
    // rewritten from scratch every tick, and writing into the buffer a frame
    // still on the GPU is reading tears the picture — so the sets rotate
    // instead of being synchronised against.
    struct FrameBuffers
    {
        std::optional<GPU::Buffer> vertices;
        std::optional<GPU::Buffer> indices;
    };

    struct DrawCommand
    {
        Graphics::Rect scissor;
        const GPU::Texture* texture = nullptr;

        // A user callback recorded in place rather than run during prepare(),
        // so it lands between the draws that surround it instead of a whole
        // frame ahead of them. Both null for an ordinary draw.
        const ImDrawList* callbackList = nullptr;
        const ImDrawCmd* callbackCommand = nullptr;

        int firstIndex = 0;
        int indexCount = 0;

        // Where this list's vertices start in the shared vertex buffer, plus
        // whatever ImGui already split off as a VtxOffset. Handed to the draw
        // rather than added into the index values, which is what keeps them at
        // ImDrawIdx's own width.
        int baseVertex = 0;
    };

    // Matches the deepest pipeline either backend runs (Metal's default
    // drawable pool is three; DXGI's present queue is two).
    static constexpr int framesInFlight = 3;

    void updateTexture(ImTextureData& texture);
    void createTexture(ImTextureData& texture);
    void uploadRegions(ImTextureData& texture);
    void destroyTexture(ImTextureData& texture);

    void appendList(const ImDrawList& list, ImVec2 origin, ImVec2 scale);
    void appendVertices(const ImDrawList& list);
    void appendIndices(const ImDrawList& list, const ImDrawCmd& command);

    void uploadGeometry();
    void ensureCapacity(std::optional<GPU::Buffer>& buffer,
                        std::size_t bytes,
                        GPU::BufferUsage usage);

    void bindState(GPU::RenderPass& pass, FrameBuffers& buffers);

    DrawShader shader;

    Vector<DrawVertex> vertices;
    Vector<ImDrawIdx> indices;
    Vector<DrawCommand> commands;

    // Owned rather than held by value: ImDrawCmd carries the address of one of
    // these, so the addresses have to survive the container growing.
    OwnedVector<GPU::Texture> textures;

    FrameBuffers frames[framesInFlight];
    int frameIndex = 0;
};
} // namespace eacp::Gui
