# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Git Rules

Claude must never commit or push without explicit permission from the user in
the current conversation.

## Project Overview

imgui-eacp integrates [Dear ImGui](https://github.com/ocornut/imgui) with
[eacp](https://github.com/eyalamirmusic/eacp): `eacp::Gui::ImGuiView` is a
`GPU::GPUView` subclass that runs an ImGui context and draws its output through
eacp's GPU module — Metal on Apple platforms, D3D12 on Windows.

Both dependencies come from CPM at configure time (`CMake/FindEACP.cmake`,
`CMake/FindImGui.cmake`). ImGui ships no CMakeLists of its own, so
`FindImGui.cmake` fetches it `DOWNLOAD_ONLY` and builds the `imgui` target here.

## Build Commands

```bash
# Configure (always pass -DEACP_UNITY_BUILD=OFF for working LSP)
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug -DEACP_UNITY_BUILD=OFF

# Build everything
cmake --build build

# Build one app
cmake --build build --target Demo
cmake --build build --target MixedViews
```

Output:
- `build/Apps/Demo/Demo.app` (macOS bundle)
- `build/Apps/MixedViews/MixedViews.app`

### Local eacp checkout

eacp is fetched from GitHub `main` by default. When co-developing both repos,
override the CPM source at configure time:

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug -DEACP_UNITY_BUILD=OFF \
      -DCPM_EACP_SOURCE=$HOME/Code/eacp
```

Use `$HOME` (not `~`). CMake does not expand `~`, and shell tilde expansion is
suppressed inside quotes — `-DCPM_EACP_SOURCE="~/Code/eacp"` silently
configures against a non-existent path and fails later with an unrelated error.

## Architecture

```
imgui-eacp/
  Lib/imgui-eacp/        The integration library (target: imgui-eacp)
    View/ImGuiView       GPUView subclass: context, frame timing, input
    Renderer/DrawRenderer  ImDrawData -> eacp GPU calls, plus the shader
    Input/KeyMap         eacp key codes / buttons / cursors -> ImGui's
    ImGuiEacp.h          Umbrella header, the only one apps include
  Apps/                  Example applications (one subdir per app)
  CMake/                 CPM.cmake + Find modules + warnings interface
```

New source files are added directly to `Lib/imgui-eacp/CMakeLists.txt` under
`target_sources(...)`. New apps go under `Apps/<Name>/` with their own
`CMakeLists.txt` and an `add_subdirectory` call in `Apps/CMakeLists.txt`.

### Namespace

Everything lives in `eacp::Gui`, nested inside eacp's own namespace the way
`eacp::Sprites` and `eacp::Cameras` are — so `GPU::`, `Graphics::` and
`Threads::` resolve unqualified inside this library, exactly as they do in
eacp's own modules. Deliberately not `eacp::ImGui`, which would shadow the
global `ImGui` namespace for every call inside it.

### Things that are the way they are for a reason

- **prepare() / encode() are separate.** Both GPU backends want texture creates,
  texture uploads and buffer rewrites to happen with no encoder open, and a
  `RenderPass` is an open encoder for its whole lifetime. `prepare()` runs
  before `Frame::beginPass`; `encode()` runs inside the pass.
- **Geometry goes through `GPU::StreamingBuffers`.** It is rewritten every frame,
  and writing into a buffer a frame still on the GPU is reading tears the
  picture — `Buffer::update` does not synchronise against frames in flight. The
  stream hands back a buffer no in-flight frame is reading and recycles it once
  that frame cannot be, so steady state allocates nothing. `DrawRenderer` used
  to roll its own rotation; don't bring it back.
- **`ImDrawIdx`-width indices, copied verbatim.** One vertex buffer serves every
  draw list, and `RenderPass::drawIndexed`'s `baseVertex` says where each list
  starts in it — so nothing is added into the index values and they stay 16-bit.
  That is also why `ImGuiBackendFlags_RendererHasVtxOffset` can be set. Don't
  reintroduce rebasing: it doubles the index buffer and costs a pass over every
  index in the frame.
- **Sampling is on the shader, not the texture.** `GPU::TextureSampling` is
  baked in at compile time (a Windows driver bug — see eacp's `SAMPLERS.md`), so
  `drawSampling` is one constant used both by `DrawShader`'s constructor and by
  every `setFragmentTexture` call. Changing one without the other makes the two
  backends draw differently.
- **The destructor clears `io.Backend*`.** `ImGui::Shutdown` asserts that a
  backend unregistered itself, and the assert fires on every debug-build exit
  if it doesn't.
- **`ContextScope` on every entry point.** Each view owns a context; a public
  method that does not make its own current will read or write another view's.
- **A system font is loaded by default.** ImGui's built-in ProggyClean is a
  bitmap-style font baked for exactly 13px with no antialiasing, so at any other
  size — and on a Retina panel especially — the whole UI reads as low
  resolution. `ViewOptions::fontPath` defaults to SF Pro / Segoe UI, with
  `ImFontFlags_NoLoadError` so a machine missing the file falls back rather than
  asserting. Don't "simplify" this back to the default font.

## Code Style

- Modern C++20, RAII everywhere.
- Use `auto` for variables whenever possible. Use explicit return types for
  functions and member functions.
- Don't write comments unless absolutely needed — prefer self-documenting names.
  Where a comment is warranted, it explains *why*, not what.
- struct/class members go last, below methods.
- No `m_` or `_` prefixes. Use `xToUse` for input variables that shadow members.
- Give `std::function` members a non-null default — a no-op lambda — so call
  sites invoke them directly without null checks.
- Allman braces, 4-space indent, 85-column limit, left-aligned pointers —
  enforced by `.clang-format` / `.clang-tidy` (both copied from eacp).
- Always run clang-format on edited source files.

ImGui's own headers are included as SYSTEM so they don't trip this project's
`-Wall -Wextra -Wpedantic`; don't relax the warning level to accommodate them.
