# imgui-eacp

[Dear ImGui](https://github.com/ocornut/imgui) as an
[eacp](https://github.com/eyalamirmusic/eacp) `View`, drawn by eacp's GPU
module — Metal on Apple platforms, D3D12 on Windows, with nothing to select.

```cpp
#include <imgui-eacp/ImGuiEacp.h>

using namespace eacp;

struct MyApp
{
    MyApp()
    {
        view.onDraw = [] { ImGui::ShowDemoWindow(); };
        window.setContentView(view);
    }

    Gui::ImGuiView view;
    Graphics::Window window;
};

int main() { return Apps::run<MyApp>(); }
```

`Gui::ImGuiView` is an ordinary view in the eacp hierarchy: give it bounds, put
it beside a `WebView`, stack a shape layer over it, snapshot it with
`renderToImage`. It owns its own `ImGuiContext`, so two of them in one window
keep separate windows, settings and input state.

## What's here

- `Lib/imgui-eacp/` — the integration itself.
  - `View/ImGuiView` — the `GPUView` subclass: context lifetime, frame timing,
    mouse, keyboard, cursors, clipboard.
  - `Renderer/DrawRenderer` — `ImDrawData` turned into eacp GPU calls, with
    ImGui's shader written once in eacp's shader EDSL so one definition emits
    both MSL and HLSL.
  - `Input/KeyMap` — eacp key codes, mouse buttons and cursors mapped to ImGui's.
- `Apps/Demo/` — the ImGui demo window plus a panel reporting what the backend
  sees: view size, backing scale, sample count, per-frame geometry.
- `Apps/MixedViews/` — an ImGui panel and a `WebView` side by side in one
  window, separated by a draggable splitter, wired to each other in both
  directions.
- `Apps/Bench/` — what a frame costs, against a synthetic load set by sliders
  rather than by whatever the demo window happens to contain. Build it
  `RelWithDebInfo`: `prepare()` reads 13× slower in a Debug build.
- `Apps/Model/` — a glTF inspector. eacp's `Mesh` module loads and draws the
  model; this backend's overlay reports its node tree, materials, geometry cost
  and per-pass GPU timings. The 3D view is an `ImGui::Image` of a render target,
  because the UI's pass has no depth attachment and a scene needs one — see
  `ImGuiView::onBeforePass`.

## Building

CMake 3.31+ and a C++20 toolchain. eacp and Dear ImGui are fetched by CPM at
configure time; nothing needs installing.

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug -DEACP_UNITY_BUILD=OFF
cmake --build build
```

Output:

- `build/Apps/Demo/Demo.app` (macOS bundle)
- `build/Apps/MixedViews/MixedViews.app`
- `build/Apps/Bench/Bench.app`
- `build/Apps/Model/Model.app`

To build against a local eacp checkout — the usual case when the two are being
developed together:

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug -DEACP_UNITY_BUILD=OFF \
      -DCPM_EACP_SOURCE=$HOME/Code/eacp
```

Use `$HOME`, not `~`. CMake does not expand `~`, and shell tilde expansion is
suppressed inside quotes, so `-DCPM_EACP_SOURCE="~/Code/eacp"` silently
configures against a path that does not exist.

## How it works

**Rendering.** ImGui's shader is a `GPU::ShaderProgram` — a C++ struct with
uniform members and a `define()` body, not a string literal per backend — so
Metal and D3D12 cannot drift apart on it. Each frame is two steps rather than
one: `prepare()` creates and uploads textures and rewrites the geometry, then
`encode()` records the draws. They are separate because both backends want
their uploads outside an open encoder, and a `RenderPass` is an open encoder
for the whole of its lifetime.

Geometry goes through `GPU::StreamingBuffers`. The UI is rebuilt from scratch
every tick, and writing into a buffer a frame still on the GPU is reading tears
the picture, so each frame is handed a buffer no in-flight frame is reading —
recycled once that frame can no longer be on the GPU, which means a steady UI
allocates nothing. The demo panel's "GPU buffers created" is that claim,
reported live.

Every draw list shares one vertex stream, and each draw carries its own base
vertex to say where its list starts in it — so the indices are copied through
untouched and stay `ImDrawIdx`-wide, 16 bits by default. That is also what lets
`ImGuiBackendFlags_RendererHasVtxOffset` be set, so ImGui keeps a window's
geometry in one draw list past 64k vertices.

`ImGuiBackendFlags_RendererHasTextures` is set too, so the font atlas grows one
glyph at a time through `Texture::update`'s region overload instead of being
rebuilt and re-sent whole.

**Coordinates.** Everything ImGui is handed is in logical points, like the rest
of eacp: `io.DisplaySize` is the view's bounds and `io.DisplayFramebufferScale`
is `GPUView::backingScale()`. The atlas is rasterized at that density, so a 15pt
font is 15pt on a Retina panel and on a conventional one. Only the scissor rect
is in pixels, because that is what both backends' scissor state means.

**Fonts.** `ViewOptions::fontPath` defaults to the platform's UI font — SF Pro
on macOS, Segoe UI on Windows — and falls back to ImGui's built-in font if the
file cannot be read. This is deliberate rather than incidental: ImGui's default
is ProggyClean, a bitmap-style font drawn for exactly 13px with no
antialiasing. It is crisp at that one size and blocky and soft at every other,
which reads as a low-resolution app on a Retina panel no matter how correct the
DPI pipeline is. Point `fontPath` at your own TTF to override.

**The ImTextureID contract.** It is a `GPU::Texture*`. To draw your own texture,
pass `Gui::DrawRenderer::toTextureID(myTexture)` to `ImGui::Image` and keep the
texture alive for the frame that draws it.

## Known gaps

- Window activation is not wired to `io.AddFocusEvent` from the window itself;
  the view polls its own `hasFocus()` instead, which is per-view keyboard focus
  rather than per-window activation.
- No IME hook (`Platform_SetImeDataFn`), so composing scripts fall back to the
  platform's own input handling.
- eacp's `MouseCursor` has no diagonal resize shapes, so ImGui's two corner
  grips come back as the arrow.
- Multi-viewport is out of scope: this is the master branch of ImGui, one
  viewport per view.

## License

MIT — see [LICENSE](LICENSE). Dear ImGui is MIT; eacp is MIT.
