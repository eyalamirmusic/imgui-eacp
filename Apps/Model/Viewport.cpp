#include "Viewport.h"

#include <algorithm>

namespace ModelApp
{
using namespace eacp::Mesh;

namespace
{
// Big enough for a panel dragged to fill a large display at 2x, and a ceiling
// rather than a guess: a panel resized by a drag asks for a new texture on every
// frame of that drag, and one absurd width would try to allocate it.
constexpr auto maximumSide = 8192;
} // namespace

bool Viewport::resize(int width, int height)
{
    auto clampedWidth = std::clamp(width, 0, maximumSide);
    auto clampedHeight = std::clamp(height, 0, maximumSide);

    if (clampedWidth <= 0 || clampedHeight <= 0)
        return false;

    if (target.has_value() && clampedWidth == sizeValue[0]
        && clampedHeight == sizeValue[1])
        return target->isValid();

    // renderTarget so a pass can draw into it, depth because the model needs a
    // depth test, and RGBA8Unorm rather than the drawable's BGRA - the renderer
    // is told which via pixelFormatFor, and the two only have to agree with each
    // other.
    auto descriptor =
        GPU::TextureDescriptor {.width = clampedWidth,
                                .height = clampedHeight,
                                .format = GPU::TextureFormat::RGBA8Unorm,
                                .renderTarget = true,
                                .depth = true};

    target.emplace(GPU::Device::shared(), descriptor, nullptr);

    sizeValue[0] = clampedWidth;
    sizeValue[1] = clampedHeight;

    return target->isValid();
}

float Viewport::aspect() const
{
    return sizeValue[1] > 0 ? (float) sizeValue[0] / (float) sizeValue[1] : 1.0f;
}

void Viewport::render(GPU::Frame& frame,
                      MeshRenderer& renderer,
                      const RenderOptions& options)
{
    if (!isValid())
        return;

    // A pass on the frame that was already given, not a frame of its own. It
    // runs before the UI's pass on the same command buffer, so what the UI
    // samples is this frame's picture.
    //
    // Labelled, which is what asks eacp's GPU timer to measure it. Both this
    // pass and the UI's carry one, so the Frame panel can say how much of the
    // frame went on the model and how much on the inspector drawn over it —
    // which is the obvious question about a tool that renders itself.
    auto pass = frame.beginPass(
        *target, {.clearColor = clearColor, .clear = true, .label = "model"});

    renderer.draw(pass, options);
}
} // namespace ModelApp
