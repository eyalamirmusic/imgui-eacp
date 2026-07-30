#include "Common.h"

// One context per view, and the ContextScope that makes it work.
//
// This is the architectural claim in ImGuiView's header — "each view owns its
// own ImGuiContext... every entry point makes that context current for the
// duration and restores whatever was current before, which is what lets the
// second view work at all". Until this file existed the only thing standing
// behind it was the MixedViews app looking right to whoever ran it.
//
// A missing scope does not crash. It reads or writes another view's state, so
// the symptom is one panel's windows appearing in the other, or input going to
// the wrong one — which is why every case here uses *two* views that disagree.

using namespace nano;
using namespace eacp;
using namespace eacp::Gui;
using namespace eacp::Gui::Tests;

namespace
{
constexpr auto red = Graphics::Color {1.0f, 0.0f, 0.0f};
constexpr auto blue = Graphics::Color {0.0f, 0.0f, 1.0f};
} // namespace

// Two views alive at once keep separate contexts. Distinct pointers is the
// weakest form of the claim and the one everything else rests on.
auto tEachViewOwnsAContext = test("Context/eachViewOwnsAContext") = []
{
    auto first = RectView {red, IM_COL32_WHITE, {0, 0}, {0, 0}};
    auto second = RectView {blue, IM_COL32_WHITE, {0, 0}, {0, 0}};

    check(first.getContext() != nullptr);
    check(second.getContext() != nullptr);
    check(first.getContext() != second.getContext());
};

// A public entry point restores whatever context was current before it. An app
// with its own ImGui context — or simply two of these views — depends on this:
// a method that leaves its own context current turns the *next* unrelated
// ImGui:: call into a write to the wrong context.
auto tEntryPointsRestoreTheCurrentContext =
    test("Context/entryPointsRestoreTheCurrentContext") = []
{
    auto* outer = ImGui::CreateContext();
    ImGui::SetCurrentContext(outer);

    // This context stands in for an app's own, so it is a plain one — but it
    // must not persist settings, or the suite drops an imgui.ini in whatever
    // directory it ran from.
    ImGui::GetIO().IniFilename = nullptr;

    {
        auto view = RectView {red, IM_COL32_WHITE, {0, 0}, {0, 0}};
        view.setBounds({0.0f, 0.0f, 32.0f, 32.0f});

        check(ImGui::GetCurrentContext() == outer);

        view.wantsMouse();
        check(ImGui::GetCurrentContext() == outer);

        view.wantsKeyboard();
        check(ImGui::GetCurrentContext() == outer);

        auto event = Graphics::MouseEvent {};
        event.pos = {4.0f, 4.0f};
        view.mouseMoved(event);
        check(ImGui::GetCurrentContext() == outer);

        if (hasDevice())
        {
            view.renderToImage(view.backingScale());
            check(ImGui::GetCurrentContext() == outer);
        }
    }

    ImGui::DestroyContext(outer);
    ImGui::SetCurrentContext(nullptr);
};

// The one that would actually catch a missing scope: two views drawing
// different things, rendered alternately. If either leaked its context, the
// second render would draw with the first's state and the colours would cross.
auto tTwoViewsDoNotCrossContaminate =
    test("Context/twoViewsDoNotCrossContaminate") = []
{
    if (!hasDevice())
        return;

    // Green rect on red, in the top-left; blue background with no rect at all.
    auto first = RectView {red, IM_COL32(0, 255, 0, 255), {0, 0}, {32, 32}};
    auto second = RectView {blue, IM_COL32_WHITE, {0, 0}, {0, 0}};

    first.setBounds({0.0f, 0.0f, 64.0f, 64.0f});
    second.setBounds({0.0f, 0.0f, 64.0f, 64.0f});

    const auto scale = first.backingScale();

    auto at = [&](const Graphics::Image& image, float x, float y)
    { return image.at((int) (x * scale), (int) (y * scale)); };

    // Interleaved rather than one after the other, because a context left
    // current survives until something else sets one — so A, B, A is the order
    // that makes a leak visible.
    auto firstImage = first.renderToImage(scale);
    auto secondImage = second.renderToImage(scale);
    auto firstAgain = first.renderToImage(scale);

    check(firstImage.isValid());
    check(secondImage.isValid());
    check(firstAgain.isValid());

    check(near(at(firstImage, 16, 16), 0, 255, 0));
    check(near(at(firstImage, 48, 48), 255, 0, 0));

    // The second view has no rect: blue everywhere, including where the first
    // view's rect would be.
    check(near(at(secondImage, 16, 16), 0, 0, 255));
    check(near(at(secondImage, 48, 48), 0, 0, 255));

    // And the first view is unchanged by having rendered the second in between.
    check(near(at(firstAgain, 16, 16), 0, 255, 0));
    check(near(at(firstAgain, 48, 48), 255, 0, 0));

    check(first.draws == 2);
    check(second.draws == 1);
};
