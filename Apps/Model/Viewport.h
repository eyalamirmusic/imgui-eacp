#pragma once

#include <eacp/Mesh/Mesh.h>

#include <optional>

// The texture the model is drawn into, and which the UI then shows.
//
// A texture rather than the drawable, because the two want different passes:
// ImGui's pipeline has no depth attachment and a 3D scene needs one, and both
// backends reject a draw whose pipeline disagrees with its pass about that. So
// the scene gets a pass of its own, on a target with depth, and the UI samples
// the result — which is also what makes the viewport a resizable panel rather
// than the whole window.

namespace ModelApp
{
using namespace eacp;

class Viewport
{
public:
    // Resizes to match the panel, reallocating only when the size actually
    // changed. Returns false when there is nothing to draw into - a panel
    // collapsed to nothing, or a device that refused the texture.
    bool resize(int width, int height);

    // Renders the model into the texture. Must be called with no encoder open,
    // which is what ImGuiView::onBeforePass gives.
    void render(GPU::Frame& frame,
                Mesh::MeshRenderer& renderer,
                const Mesh::RenderOptions& options);

    bool isValid() const { return target.has_value() && target->isValid(); }

    const GPU::Texture& texture() const { return *target; }

    int width() const { return sizeValue[0]; }
    int height() const { return sizeValue[1]; }

    float aspect() const;

    Graphics::Color clearColor {0.10f, 0.11f, 0.14f};

private:
    std::optional<GPU::Texture> target;
    int sizeValue[2] {0, 0};
};
} // namespace ModelApp
