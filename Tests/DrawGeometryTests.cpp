#include "Common.h"

// The geometry path, end to end, read back as pixels.
//
// These drive a real ImGuiView through View::renderToImage: ImGui builds
// ImDrawData, DrawRenderer turns it into buffers and draws, and the off-screen
// frame hands back an image. That is the only way to check most of what this
// backend does — a clip rect, a base vertex and a packed colour have no
// CPU-side observable, and every one of them fails as wrong pixels rather than
// as an error.
//
// It is also what makes a claim about the D3D12 side checkable. Until this
// file existed, CI on Windows compiled this repository and ran nothing.

using namespace nano;
using namespace eacp;
using namespace eacp::Gui;
using namespace eacp::Gui::Tests;

namespace
{
constexpr auto black = Graphics::Color {0.0f, 0.0f, 0.0f};

// ImGui's ABGR packing, which is what IM_COL32 builds and what the vertex
// carries. Written out so a case names its colour in RGB order and the packing
// is in one place.
ImU32 rgb(int r, int g, int b)
{
    return IM_COL32(r, g, b, 255);
}
} // namespace

// The clear colour, with nothing drawn over it. The cheapest possible check
// that the view renders at all, and the baseline every other case here reads
// against.
auto tClearColorReachesTheImage = test("DrawGeometry/clearColorReachesTheImage") = []
{
    if (!hasDevice())
        return;

    auto view = RectView {{0.0f, 0.0f, 1.0f}, rgb(0, 0, 0), {0, 0}, {0, 0}};
    view.setBounds({0.0f, 0.0f, 64.0f, 64.0f});

    auto image = view.renderToImage(1.0f);

    check(image.isValid());
    check(image.width() == 64);
    check(image.height() == 64);
    check(near(image.at(32, 32), 0, 0, 255));
};

// A filled rectangle at named coordinates: inside it is the fill, outside it is
// the clear. Both halves are the case — a renderer drawing the rect over the
// whole target passes the first check on its own.
auto tFilledRectLandsWhereItIsAsked =
    test("DrawGeometry/filledRectLandsWhereItIsAsked") = []
{
    if (!hasDevice())
        return;

    // Green over blue, with the rect covering the top-left quadrant.
    auto view = RectView {{0.0f, 0.0f, 1.0f}, rgb(0, 255, 0), {0, 0}, {32, 32}};
    view.setBounds({0.0f, 0.0f, 64.0f, 64.0f});

    auto image = view.renderToImage(1.0f);

    check(image.isValid());

    // Well inside the rect, away from the antialiased edge.
    check(near(image.at(16, 16), 0, 255, 0));

    // And the three quadrants it must not have touched. ImGui's y axis points
    // down and the snapshot's does too, so (16, 48) is below the rect — a
    // backend that flipped y would put the fill there and pass a case that only
    // sampled the middle.
    check(near(image.at(48, 16), 0, 0, 255));
    check(near(image.at(16, 48), 0, 0, 255));
    check(near(image.at(48, 48), 0, 0, 255));
};

// The colour survives the packing. DrawVertex stores it as a UNorm8x4 — the
// four bytes ImGui already packed — and the shader reads a Float4, with the
// widening done by the vertex fetch. A channel swap there is invisible in grey
// and obvious in a colour with three different channels.
auto tVertexColorSurvivesPacking =
    test("DrawGeometry/vertexColorSurvivesPacking") = []
{
    if (!hasDevice())
        return;

    auto view = RectView {black, rgb(255, 128, 0), {0, 0}, {64, 64}};
    view.setBounds({0.0f, 0.0f, 64.0f, 64.0f});

    auto image = view.renderToImage(1.0f);

    check(image.isValid());

    // Orange: r high, g mid, b zero. A red/blue swap - the classic packing bug,
    // and the one that looks fine on one backend and wrong on the other - lands
    // on (0, 128, 255) and fails every one of these.
    check(near(image.at(32, 32), 255, 128, 0, 3));
};

// The scissor rect, which is what an ImDrawCmd's ClipRect becomes. A clipped
// draw has no observable but the pixels it did not write, so this is the only
// shape the case can take.
//
// Rendered at the view's *own* backing scale rather than at 1. ImGuiView derives
// io.DisplayFramebufferScale from backingScale(), and the scissor rect is a clip
// rect multiplied by it — so a snapshot taken at any other scale computes the
// scissor for a target of a different size, and on a 2x display a 1x snapshot
// gets a scissor twice too large, which clips nothing. That is a real defect on
// the snapshot path and it is written up in the README's known gaps; here the
// two scales are made to agree so this case tests clipping rather than that.
auto tClipRectClipsTheDraw = test("DrawGeometry/clipRectClipsTheDraw") = []
{
    if (!hasDevice())
        return;

    class ClippedView final : public ImGuiView
    {
    public:
        ClippedView()
            : ImGuiView(options())
        {
        }

    protected:
        void draw() override
        {
            auto* list = ImGui::GetBackgroundDrawList();

            // A full-window rect, clipped to the left half. What lands is the
            // intersection, so the right half keeps the clear colour — and a
            // backend ignoring the clip rect fills the whole target instead.
            list->PushClipRect(ImVec2 {0, 0}, ImVec2 {32, 64}, false);
            list->AddRectFilled(
                ImVec2 {0, 0}, ImVec2 {64, 64}, IM_COL32(0, 255, 0, 255));
            list->PopClipRect();
        }

    private:
        static ViewOptions options()
        {
            auto result = ViewOptions {};
            result.clearColor = {0.0f, 0.0f, 1.0f};
            result.fontPath = "none";
            return result;
        }
    };

    auto view = ClippedView {};
    view.setBounds({0.0f, 0.0f, 64.0f, 64.0f});

    const auto scale = view.backingScale();
    auto image = view.renderToImage(scale);

    check(image.isValid());

    // Sampled in pixels, so every coordinate carries the scale the snapshot was
    // taken at. The clip boundary is at 32 points, whatever that is in pixels.
    auto at = [&](float xPoints, float yPoints)
    { return image.at((int) (xPoints * scale), (int) (yPoints * scale)); };

    check(near(at(16, 32), 0, 255, 0)); // inside the clip: filled
    check(near(at(48, 32), 0, 0, 255)); // outside it: still the clear
};

// Backing scale. The snapshot is taken at a density, and ImGui works in logical
// points — so a rect named in points has to cover the same *fraction* of the
// image whatever the scale, and the image itself has to grow.
//
// This is the property a Retina panel exercises constantly and a test machine
// at 1x never would.
auto tGeometryFollowsBackingScale =
    test("DrawGeometry/geometryFollowsBackingScale") = []
{
    if (!hasDevice())
        return;

    auto view = RectView {{0.0f, 0.0f, 1.0f}, rgb(0, 255, 0), {0, 0}, {32, 32}};
    view.setBounds({0.0f, 0.0f, 64.0f, 64.0f});

    auto image = view.renderToImage(2.0f);

    check(image.isValid());
    check(image.width() == 128);
    check(image.height() == 128);

    // The rect is still the top-left quadrant: 32 of 64 points is 64 of 128
    // pixels. A backend that scaled the geometry but not the projection - or
    // neither - puts the boundary at 32 pixels and fails the second of these.
    check(near(image.at(32, 32), 0, 255, 0));
    check(near(image.at(96, 32), 0, 0, 255));
    check(near(image.at(32, 96), 0, 0, 255));
};

// Nothing drawn at all. ImGui still produces an ImDrawData with zero lists, and
// the renderer has to cope: no geometry means no buffer was written this frame,
// and encode() binding last frame's buffer - or a null one - is a crash rather
// than a wrong pixel.
auto tEmptyFrameDrawsTheClear = test("DrawGeometry/emptyFrameDrawsTheClear") = []
{
    if (!hasDevice())
        return;

    class EmptyView final : public ImGuiView
    {
    public:
        EmptyView()
            : ImGuiView(options())
        {
        }

    protected:
        void draw() override {}

    private:
        static ViewOptions options()
        {
            auto result = ViewOptions {};
            result.clearColor = {1.0f, 0.0f, 0.0f};
            result.fontPath = "none";
            return result;
        }
    };

    auto view = EmptyView {};
    view.setBounds({0.0f, 0.0f, 32.0f, 32.0f});

    auto image = view.renderToImage(1.0f);

    check(image.isValid());
    check(near(image.at(16, 16), 255, 0, 0));
};
