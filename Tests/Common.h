#pragma once

#include <imgui-eacp/ImGuiEacp.h>

#include <NanoTest/NanoTest.h>

#include <cmath>

// Shared scaffolding for the imgui-eacp tests.
//
// The rendered cases drive an ImGuiView through View::renderToImage, which runs
// the off-screen path with no window on screen — the same mechanism eacp's GPU
// suite uses, and the reason every claim here is checkable on both backends in
// CI rather than only on the machine it was written on.

namespace eacp::Gui::Tests
{
// Whether a snapshot pixel is the colour a case expects.
//
// The tolerance is not politeness: the snapshot path returns premultiplied
// pixels that have been through an 8-bit round trip, and ImGui antialiases the
// edge of everything it draws. Cases therefore sample the *middle* of a filled
// area rather than near an edge, and the tolerance covers the round trip only.
inline bool
    near(const Graphics::Color& color, int r, int g, int b, int tolerance = 2)
{
    auto within = [&](float channel, int target)
    { return std::abs((int) std::lround(channel * 255.0f) - target) <= tolerance; };

    return within(color.r, r) && within(color.g, g) && within(color.b, b);
}

// Whether there is a GPU to render with. Every rendered case returns early
// without one rather than failing, so the suite still says something useful on
// a machine or runner with no device.
inline bool hasDevice()
{
    return GPU::Device::shared().isValid();
}

// An ImGuiView that draws one full-window rectangle in a known colour, with no
// decoration, no padding and no rounding, positioned in absolute coordinates.
//
// Written this way because the assertions are about pixels: a default ImGui
// window brings a title bar, a border, a rounded corner and a background of its
// own, and every one of those changes what a given coordinate holds. What is
// under test is the geometry path, so the geometry is made trivial and the
// colour is made exact.
class RectView final : public ImGuiView
{
public:
    RectView(const Graphics::Color& clearColor,
             ImU32 rectColor,
             const ImVec2& topLeft,
             const ImVec2& bottomRight)
        : ImGuiView(makeOptions(clearColor))
        , fillColor(rectColor)
        , from(topLeft)
        , to(bottomRight)
    {
    }

    int draws = 0;

protected:
    void draw() override
    {
        ++draws;

        // Drawn on the background list rather than in a window: it needs no
        // window to exist, takes no style, and lands at the absolute
        // coordinates the case names.
        ImGui::GetBackgroundDrawList()->AddRectFilled(from, to, fillColor);
    }

private:
    static ViewOptions makeOptions(const Graphics::Color& clearColor)
    {
        auto options = ViewOptions {};
        options.clearColor = clearColor;

        // The built-in font, deliberately. Loading SF Pro or Segoe UI makes the
        // atlas depend on which machine and which OS version the test runs on,
        // and these cases assert on an atlas upload.
        options.fontPath = "none";
        return options;
    }

    ImU32 fillColor;
    ImVec2 from;
    ImVec2 to;
};
} // namespace eacp::Gui::Tests
