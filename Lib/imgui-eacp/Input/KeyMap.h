#pragma once

#include <eacp/Graphics/Graphics.h>

#include <imgui.h>

#include <cstdint>

namespace eacp::Gui
{
// eacp reports the same key code table on every platform — the Windows backend
// translates its virtual keys into it — so one mapping serves both backends,
// and a shortcut bound here works on a layout that produces other characters.
ImGuiKey toImGuiKey(std::uint16_t keyCode);

ImGuiMouseButton toImGuiButton(Graphics::MouseButton button);

// The pointer shape ImGui is asking for. eacp names the shapes every platform
// agrees on, which is a smaller set than ImGui's: the two diagonal resize
// grips have no portable spelling and come back as the arrow.
Graphics::MouseCursor toMouseCursor(ImGuiMouseCursor cursor);

void addModifiers(ImGuiIO& io, const Graphics::ModifierKeys& modifiers);
} // namespace eacp::Gui
