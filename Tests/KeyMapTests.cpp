#include "Common.h"

// The input mapping tables. Pure functions over two enumerations, which makes
// them the cheapest thing in this repository to test and the least excusable to
// have left untested — a wrong entry here is a key that does nothing or a
// modifier that sticks down, and neither is a build error on either platform.
//
// Each group asserts that known inputs land where they should *and* that
// distinct inputs stay distinct. The second half is the one that catches a
// copy-paste: a table returning the same answer twice passes every case that
// only checks one entry at a time.

using namespace nano;
using namespace eacp;
using namespace eacp::Gui;
using namespace eacp::Graphics::KeyCode;

auto tNamedKeysMap = test("KeyMap/namedKeysMap") = []
{
    check(toImGuiKey(Escape) == ImGuiKey_Escape);
    check(toImGuiKey(Return) == ImGuiKey_Enter);
    check(toImGuiKey(Tab) == ImGuiKey_Tab);
    check(toImGuiKey(Space) == ImGuiKey_Space);
    check(toImGuiKey(Home) == ImGuiKey_Home);
};

// The one pair in the table whose names invite being swapped, and the swap is
// silent: eacp's Delete is the key above Return — backspace, deleting to the
// left — while ForwardDelete is the one that deletes to the right. Both are
// "delete" in ordinary speech and ImGui spells them the other way round from
// the platform, so this is the entry most likely to be wrong and the least
// likely to be noticed in a build.
auto tDeleteAndBackspaceAreNotSwapped =
    test("KeyMap/deleteAndBackspaceAreNotSwapped") = []
{
    check(toImGuiKey(Delete) == ImGuiKey_Backspace);
    check(toImGuiKey(ForwardDelete) == ImGuiKey_Delete);

    check(toImGuiKey(Delete) != toImGuiKey(ForwardDelete));
};

auto tArrowKeysDoNotCollide = test("KeyMap/arrowKeysDoNotCollide") = []
{
    check(toImGuiKey(LeftArrow) == ImGuiKey_LeftArrow);
    check(toImGuiKey(RightArrow) == ImGuiKey_RightArrow);
    check(toImGuiKey(UpArrow) == ImGuiKey_UpArrow);
    check(toImGuiKey(DownArrow) == ImGuiKey_DownArrow);

    // Stated separately, because four arrows copy-pasted to the same entry pass
    // each of the checks above individually. The symptom is a caret that moves
    // one way whichever arrow is pressed, and it reaches a user rather than a
    // compiler.
    check(toImGuiKey(LeftArrow) != toImGuiKey(RightArrow));
    check(toImGuiKey(UpArrow) != toImGuiKey(DownArrow));
    check(toImGuiKey(LeftArrow) != toImGuiKey(UpArrow));
};

// The keypad reports its own codes precisely so an app can bind them apart from
// the number row, and folding the two together here would throw that away one
// level below the app.
auto tKeypadIsDistinctFromTheNumberRow =
    test("KeyMap/keypadIsDistinctFromTheNumberRow") = []
{
    check(toImGuiKey(Num1) == ImGuiKey_1);
    check(toImGuiKey(Keypad1) == ImGuiKey_Keypad1);
    check(toImGuiKey(Num1) != toImGuiKey(Keypad1));

    check(toImGuiKey(Return) != toImGuiKey(KeypadEnter));
};

// eacp reports Unknown for a platform key outside its table — which the Windows
// backend does whenever it translates a virtual key it has no name for — so
// this is the value that actually arrives, not a hypothetical one.
auto tUnknownKeyIsNone = test("KeyMap/unknownKeyIsNone") = []
{
    check(toImGuiKey(Unknown) == ImGuiKey_None);
    check(toImGuiKey(60000) == ImGuiKey_None);
};

auto tMouseButtonsMap = test("KeyMap/mouseButtonsMap") = []
{
    check(toImGuiButton(Graphics::MouseButton::Left) == ImGuiMouseButton_Left);
    check(toImGuiButton(Graphics::MouseButton::Right) == ImGuiMouseButton_Right);
    check(toImGuiButton(Graphics::MouseButton::Middle) == ImGuiMouseButton_Middle);

    check(toImGuiButton(Graphics::MouseButton::Left)
          != toImGuiButton(Graphics::MouseButton::Right));

    // Anything else is a left click rather than an out-of-range button index.
    // ImGui indexes io.MouseDown with this, so a fourth button arriving as 3
    // would write past the end of a five-element array on a build with no
    // bounds checking.
    check(toImGuiButton(Graphics::MouseButton::Other) == ImGuiMouseButton_Left);
};

auto tCursorShapesMap = test("KeyMap/cursorShapesMap") = []
{
    check(toMouseCursor(ImGuiMouseCursor_TextInput) == Graphics::MouseCursor::IBeam);
    check(toMouseCursor(ImGuiMouseCursor_ResizeNS)
          == Graphics::MouseCursor::ResizeUpDown);
    check(toMouseCursor(ImGuiMouseCursor_ResizeEW)
          == Graphics::MouseCursor::ResizeLeftRight);
    check(toMouseCursor(ImGuiMouseCursor_Hand)
          == Graphics::MouseCursor::PointingHand);

    check(toMouseCursor(ImGuiMouseCursor_ResizeNS)
          != toMouseCursor(ImGuiMouseCursor_ResizeEW));
};

// The documented gap, stated as a case rather than only as a README line. When
// eacp grows the diagonal shapes this fails, which is the reminder to map them
// — a gap recorded only in prose is one nobody re-reads once the blocker
// clears. See the plan's §1.4 for what that will take.
auto tDiagonalCursorsFallBackToTheArrow =
    test("KeyMap/diagonalCursorsFallBackToTheArrow") = []
{
    check(toMouseCursor(ImGuiMouseCursor_ResizeNESW)
          == Graphics::MouseCursor::Default);
    check(toMouseCursor(ImGuiMouseCursor_ResizeNWSE)
          == Graphics::MouseCursor::Default);
};

namespace
{
// A context with no renderer behind it, for the two modifier cases.
//
// RendererHasTextures is the same flag DrawRenderer sets, and without it
// NewFrame asserts that a font atlas was built: ImGui assumes a backend unable
// to manage textures needs the software atlas, and this context has no backend
// at all. Claiming the capability the real one has is what makes a frame here
// legal with no GPU anywhere near it.
struct HeadlessContext
{
    HeadlessContext()
        : previous(ImGui::GetCurrentContext())
        , context(ImGui::CreateContext())
    {
        ImGui::SetCurrentContext(context);

        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2 {64.0f, 64.0f};
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

        // Or DestroyContext writes an imgui.ini into whatever directory the
        // suite was run from. A test leaving a file behind is a test that
        // behaves differently the second time.
        io.IniFilename = nullptr;
    }

    ~HeadlessContext()
    {
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous);
    }

    // ImGui turns a queued key event into readable state during NewFrame, so
    // every assertion about io.KeyShift has to be made after one — reading it
    // straight after addModifiers passes whatever the mapping did.
    void frame() const
    {
        ImGui::NewFrame();
        ImGui::EndFrame();
    }

    ImGuiIO& io() const { return ImGui::GetIO(); }

    ImGuiContext* previous;
    ImGuiContext* context;
};
} // namespace

// What the mapping actually does, with ImGui's platform translation turned off
// so that this reads the backend rather than ImGui.
auto tModifiersReachTheIo = test("KeyMap/modifiersReachTheIo") = []
{
    auto headless = HeadlessContext {};
    auto& io = headless.io();
    io.ConfigMacOSXBehaviors = false;

    headless.frame();

    auto modifiers = Graphics::ModifierKeys {};
    modifiers.shift = true;
    modifiers.control = true;

    addModifiers(io, modifiers);
    headless.frame();

    check(io.KeyShift);
    check(io.KeyCtrl);
    check(!io.KeyAlt);
    check(!io.KeySuper);

    auto command = Graphics::ModifierKeys {};
    command.command = true;
    command.alt = true;

    addModifiers(io, command);
    headless.frame();

    check(io.KeySuper);
    check(io.KeyAlt);

    // The half a case checking only the pressed state would miss: a modifier
    // never cleared stays down for the rest of the session, which reads as the
    // keyboard being broken rather than as a mapping bug.
    check(!io.KeyShift);
    check(!io.KeyCtrl);
};

// ImGui swaps Cmd and Ctrl itself when ConfigMacOSXBehaviors is set, which is
// its default on Apple — imgui.cpp's AddKeyEvent, under the comment "MacOS:
// swap Cmd(Super) and Ctrl". So an app writes ImGuiMod_Ctrl shortcuts once and
// they fire on Cmd there, which is the whole point of the flag.
//
// This is here because the backend looks wrong when you first see it: eacp's
// `command` arrives as Super, and what an app then observes is Ctrl. The
// tempting "fix" is to pre-swap the two in addModifiers — which double-swaps
// and breaks every Cmd shortcut on macOS while looking correct in isolation.
// The flag is set explicitly rather than left to the platform default so this
// asserts the same thing on both backends.
auto tImGuiSwapsCommandAndControlItself =
    test("KeyMap/imGuiSwapsCommandAndControlItself") = []
{
    auto headless = HeadlessContext {};
    auto& io = headless.io();
    io.ConfigMacOSXBehaviors = true;

    headless.frame();

    auto command = Graphics::ModifierKeys {};
    command.command = true;

    addModifiers(io, command);
    headless.frame();

    // Cmd went in; Ctrl is what a shortcut sees.
    check(io.KeyCtrl);
    check(!io.KeySuper);

    auto control = Graphics::ModifierKeys {};
    control.control = true;

    addModifiers(io, control);
    headless.frame();

    check(io.KeySuper);
    check(!io.KeyCtrl);
};
