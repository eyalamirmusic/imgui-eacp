# eacp GPU module: the gaps this backend exposes, and the plan to close them

Building this backend against eacp's GPU module surfaced three things the module
does not offer, each of which `DrawRenderer` works around in a different way.
None of the workarounds is wrong; all three belong in `eacp-gpu` instead, where
`Sprites`, `Text` and any future mesh renderer get them too.

This document is the plan to move them. It starts with the working setup,
because the whole plan involves changing two repositories at once and the build
wiring for that is the first thing to get right.

---

## Where this stands

All four phases have landed. Each has a branch of the same name in *both*
repositories, stacked so that each contains the ones before it; §3 carries what
each one actually cost and what the plan got wrong about it.

| Phase | Branch | State |
| --- | --- | --- |
| 1 — base vertex (§1.1) | `gpu-base-vertex` | Done, both backends |
| 2 — streaming buffers (§1.2) | `gpu-streaming-buffer` | Done, both backends |
| 3 — packed vertex formats (§1.3) | `gpu-packed-formats` | Done, both backends |
| 4a — `Bench`, CPU side (§2.2) | `gpu-packed-formats` | Done, this repo only |
| 4b — timestamp queries (§2.2) | `gpu-timestamps` | Done, both backends |

None of it is merged. Both repositories sit on `gpu-timestamps`, four branches
deep, and **this repository's CI cannot pass until eacp's side is on eacp
`main`** — `CMake/FindEACP.cmake` fetches `GIT_TAG main`, so CI here has been
building against an eacp with no `UNorm8x4`, no `StreamingBuffers` and no
timings since Phase 3:

```
DrawRenderer.h(21): error C2039: 'UNorm8x4': is not a member of 'eacp::GPU'
```

That is structural rather than a mistake, but it means the merge order is
eacp first, and that this repository's CI says nothing at all until then.

eacp's `GPUTests` is **159 passed, 0 failed**, up from 141 before any of this.
Every new assertion was checked against the failure it exists for by breaking
the thing deliberately and watching it fail — the base vertex pinned back to
zero, `StreamingBuffers::write` reverted to allocating per call, Metal's
`UByte4Norm` pointed at the non-normalized format, the GPU tick scale put out by
the mach timebase, a pass's two sample indices swapped. None of them is a build
error, and none shows up as anything but wrong pixels, a rising counter or a
number that is quietly the wrong size.

`DrawRenderer` is the visible result in this repo: 16-bit indices copied with
one `memcpy`, no buffer rotation of its own, and a 20-byte vertex — against
32-bit rebased indices, three hand-rolled buffer sets and a 32-byte vertex when
this document was written.

~~**Everything is Metal-only verified.**~~ **The D3D12 side runs, and is
tested.** Every phase above was written blind for D3D12 and described here as
unverified, on the assumption that a Windows machine was needed to say
otherwise. It was not: eacp's CI runners have a working D3D12 device, so
`Tests/GPU` does not self-skip there — it *renders and reads pixels back*.
`gpu-timestamps` is green at **794/794 on Windows MSVC, Windows Clang, Windows
MSVC ARM64 and Windows Clang ARM64**, `BaseVertex`, `VertexFormat` and
`FrameTiming` among them.

So the two `RenderPipeline` mapping tables in §1.3 — the part of this plan most
likely to be silently wrong, since a bad entry is not a build error on either
side — have been checked by the conformance test on the backend they were
written blind for. Worth knowing for the phases after this one: pushing a branch
*is* the Windows check, and waiting for hardware to do it was never necessary.

What CI caught that a developer machine could not is the opposite case, and it
was on Apple's side: its macOS runners have a paravirtualised GPU that cannot
sample counters at all, which is a configuration no machine here can produce.
See §3, Phase 4b.

---

## 0. Working setup

### 0.1 A dedicated eacp checkout

Every item below is a change to eacp validated by a change to imgui-eacp. That
means switching eacp branches while imgui-eacp compiles against them, which is
exactly the thing that makes `$HOME/Code/eacp` unusable for anything else — and
that tree has other work in flight (`vulkan-backend`, `video-recorder`,
`IPCLock`, the sprites branch).

Use a **git worktree** rather than a second clone. It shares the object store,
so a branch created in either tree is immediately visible in the other, and
there is no second fetch of the repository:

```bash
git -C $HOME/Code/eacp worktree add $HOME/Code/eacp-for-imgui -b gpu-base-vertex main
```

Each phase gets its own branch on that same worktree:

```bash
git -C $HOME/Code/eacp-for-imgui switch -c gpu-streaming-buffer
```

To remove it when the work has landed:

```bash
git -C $HOME/Code/eacp worktree remove $HOME/Code/eacp-for-imgui
```

A plain `git clone` works too and needs no explanation; it just costs a second
copy of the history and keeps the two trees' branch lists apart.

### 0.2 The CPM connection

`CMake/FindEACP.cmake` in this repo is three lines:

```cmake
CPMAddPackage(
        NAME EACP
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG main)
```

CPM honours `CPM_<NAME>_SOURCE`, where `<NAME>` is the `NAME` argument verbatim
— `EACP`, uppercase. So the local override is:

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug \
      -DEACP_UNITY_BUILD=OFF \
      -DCPM_EACP_SOURCE=$HOME/Code/eacp-for-imgui

cmake --build build
```

Four things worth knowing about that override:

- **`$HOME`, never `~`.** CMake does not expand tilde and shell expansion is
  suppressed inside quotes, so `-DCPM_EACP_SOURCE="~/Code/eacp-for-imgui"`
  silently configures against a path that does not exist and fails much later
  with an unrelated error.
- **It is sticky in the cache.** `grep CPM_EACP_SOURCE build/CMakeCache.txt`
  confirms which eacp is actually being compiled. Deleting the build directory
  is the only reliable way back to the GitHub fetch.
- **`GIT_TAG main` is ignored when the source is overridden.** CPM uses the
  directory exactly as it finds it, so whatever branch the worktree is sitting
  on is what imgui-eacp compiles. That is the point, but it also means a
  forgotten `git switch` in the worktree quietly changes what this repo builds.
- **`-DEACP_UNITY_BUILD=OFF` is not optional here.** Unity builds collapse the
  per-file compile commands that `compile_commands.json` and the LSP need.

### 0.3 Two build trees, deliberately

eacp's root `CMakeLists.txt` has:

```cmake
option(EACP_ENABLE_TESTS "Enable Tests for EACP" ${PROJECT_IS_TOP_LEVEL})
```

So configuring imgui-eacp does **not** build eacp's `GPUTests` — eacp is a
subproject there, not the top-level one. The same applies to
`EACP_ENABLE_EXAMPLES`, so `Apps/GPU/*` is absent too.

That is the right behaviour and the plan leans on it. Keep two trees:

| Tree | Top-level project | Builds | Used for |
| --- | --- | --- | --- |
| `$HOME/Code/eacp-for-imgui/build` | eacp | `GPUTests`, `Apps/GPU/*` | Unit-testing new GPU features |
| `$HOME/Code/imgui-eacp/build` | imgui-eacp | `Demo`, `MixedViews`, `Bench` | Proving the integration |

The loop for every item below is: change eacp → build and run `GPUTests` in
tree 1 → build tree 2 → confirm `Demo` still renders and `Bench` numbers moved
the right way.

Forcing `-DEACP_ENABLE_TESTS=ON` from imgui-eacp does work, but don't: it pulls
NanoTest into this project's configure and mixes two repositories' test targets
into one build tree for no benefit.

### 0.4 Sanity check before starting

The build directories in this repo currently point at the CPM-fetched eacp, not
a local one. Before any of the work below, confirm the backend still builds and
runs against local eacp `main`:

```bash
cmake -G Ninja -B build-local -DCMAKE_BUILD_TYPE=Debug \
      -DEACP_UNITY_BUILD=OFF \
      -DCPM_EACP_SOURCE=$HOME/Code/eacp-for-imgui
cmake --build build-local --target Demo
open build-local/Apps/Demo/Demo.app
```

If that diverges from `build/`, the divergence is a finding in its own right and
should be fixed before anything else changes.

---

## 1. What exactly is needed

Three items, ordered by size rather than value — see §3 for why the smallest
goes first.

### 1.1 Base vertex on `drawIndexed`

**The gap.** `RenderPass::drawIndexed` takes a first index but no base vertex:

```cpp
void drawIndexed(const Buffer& indices,
                 int indexCount,
                 IndexFormat format = IndexFormat::UInt32,
                 int firstIndex = 0);
```

**What this backend does about it.** `DrawRenderer::appendIndices` rebases every
index as it copies, so each draw list's vertex offset is baked into the index
values. That is what lets one vertex buffer serve every list and lets
`ImGuiBackendFlags_RendererHasVtxOffset` be set — but it forces 32-bit indices
regardless of `ImDrawIdx`, and costs an add per index over every index in the
frame.

**Why it matters past ImGui.** Packing many submeshes into one vertex buffer and
drawing each with its own base vertex is the standard mechanism for a mesh
renderer, and glTF scenes arrive in exactly that shape.

**The change.** Add the parameter to both entry points:

```cpp
void drawIndexed(const Buffer& indices,
                 int indexCount,
                 IndexFormat format = IndexFormat::UInt32,
                 int firstIndex = 0,
                 int baseVertex = 0);

void drawIndexedInstanced(const Buffer& indices,
                          int indexCount,
                          int instanceCount,
                          IndexFormat format = IndexFormat::UInt32,
                          int firstIndex = 0,
                          int firstInstance = 0,
                          int baseVertex = 0);
```

Files, and how small each edit is:

- `Lib/eacp/GPU/Frame/RenderPass.h` — the two signatures, plus a comment on what
  a base vertex means for a shared buffer.
- `Lib/eacp/GPU/Frame/RenderPass-Apple.mm` — the instanced path already passes
  `baseVertex:0` to the 8-argument `drawIndexedPrimitives:` selector, so it is a
  one-line change. The non-instanced path currently uses the 5-argument selector
  with no base vertex at all, so it moves to the 8-argument form with
  `instanceCount:1`.
- `Lib/eacp/GPU/Frame/RenderPass-Windows.cpp` — `DrawIndexedInstanced`'s fourth
  argument *is* `BaseVertexLocation` and is currently hardcoded `0` in both
  places. Two one-line changes.
- `Lib/eacp/GPU/Codegen/ShaderProgram.h` — `RenderPass::draw(Program&)` and
  `drawInstanced(Program&)` pass the new argument through as `0`; no behaviour
  change.

**What it unlocks here.** `DrawRenderer` stops rebasing, drops to
`ImDrawIdx`-width indices (16-bit by default), and the index buffer halves.

**Risk:** near zero. The parameter defaults to `0`, which is exactly today's
behaviour, so nothing existing changes.

### 1.2 A frame-transient buffer path

This is the substantial one, and the one with the widest blast radius.

**The gap.** `Buffer::update` documents it plainly:

> The new contents are seen by commands encoded after the call; update at most
> once per displayed frame, as pacing against frames still in flight is not
> synchronised here.

So there is no supported way to rewrite a buffer every frame. There are
currently **two different workarounds for this one missing thing, and they
disagree with each other**:

1. **This backend rotates.** `DrawRenderer` holds `FrameBuffers frames[3]` with
   a `frameIndex` and geometric `ensureCapacity`, so a frame writes a buffer no
   in-flight frame is reading. Steady state allocates nothing.

2. **eacp's own renderers reallocate.** `ShaderProgram::setInstances` does:

   ```cpp
   instanceBuffers[bufferIndex].emplace(
       Device::shared(), data, sizeof(I) * (std::size_t) count);
   ```

   A brand-new `GPU::Buffer` constructed on every call. `SpriteRenderer::flush()`
   and `Text::GlyphRenderer::flush()` both go through it, so **every batch flush
   of every frame allocates a GPU resource** — `newBufferWithBytes` on Metal, a
   committed resource creation on D3D12. It is correct, because a freshly
   allocated buffer cannot be one the GPU is still reading, but it is heap
   allocation in a hot path, which the house rules explicitly forbid.

**The design constraint the naive fix misses.** A single rotating buffer per
frame is not enough. `SpriteRenderer` flushes *many times per frame* through one
`SpriteShader` — on every texture change, sampling change, scissor change, and
at pass end. Each flush needs its own storage, because the earlier flush's draw
is still queued in the same command buffer and has not been submitted yet.
Today's per-flush allocation is what makes that safe. A design offering one
buffer per frame would break `Sprites` on its first use.

So the shape is **a per-frame-slot pool of recycled buffers**, not a rotating
single buffer.

**Proposed API.**

```cpp
namespace eacp::GPU
{
// Storage for data rewritten every frame.
//
// Buffer::update does not synchronise against frames still in flight, so
// writing one buffer each tick tears the picture. This keeps one pool of
// buffers per frame that may be in flight and hands out a buffer from the
// current frame's pool, recycling a pool only once the frame that used it can
// no longer be on the GPU.
//
// Several writes in one frame get several buffers: a batching renderer flushes
// many times per frame through one shader, and each flush's draw is still
// queued when the next one is written.
//
// Pools grow on demand and never shrink, so steady state allocates nothing.
class StreamingBuffers
{
public:
    explicit StreamingBuffers(BufferUsage usage);

    // Uploads bytes into a buffer no in-flight frame is reading, and returns it
    // for binding. Valid until this frame's pool is recycled.
    const Buffer& write(const void* data, std::size_t bytes);

    // How many GPU buffers exist across every pool. Flat in steady state; a
    // number that keeps climbing is the bug this type exists to prevent, and
    // what the tests in §2.1 assert on.
    int bufferCount() const;

private:
    // Matches the deepest pipeline either backend runs: Metal's default
    // drawable pool is three, DXGI's present queue is two.
    static constexpr int framesInFlight = 3;
};
} // namespace eacp::GPU
```

**Who advances the frame.** Nobody calls `nextFrame()`. `Device` keeps a
monotonically increasing frame counter bumped by `Frame`'s constructor, and
`write()` picks its pool from `Device::frameIndex() % framesInFlight`. This is
the same instinct as `RenderPass::Participant` — the difference between an app
forgetting a call and there being no call to forget. An explicit advance would
be forgotten exactly once, in exactly the renderer where the resulting tearing
is hardest to attribute.

**Files.**

- New: `Lib/eacp/GPU/Buffer/StreamingBuffers.h` / `.cpp`. Entirely portable — it
  is N `GPU::Buffer`s, a growth rule and an index. **No backend file changes at
  all**, which is what makes this a safe change to a two-backend module.
- `Lib/eacp/GPU/Device/Device.h` / `Device.cpp` — the frame counter and its
  accessor.
- `Lib/eacp/GPU/Frame/Frame-Apple.mm`, `Frame-Windows.cpp` — bump the counter in
  the constructor. One line each.
- `Lib/eacp/GPU/Codegen/ShaderProgram.h` — `setInstances` writes through a
  `StreamingBuffers` per slot instead of `emplace`-ing a fresh `Buffer`.
- `Lib/eacp/GPU/CMakeLists.txt` — the new sources under `target_sources`.

**What it fixes.**

- `Sprites::SpriteRenderer` and `Text::GlyphRenderer` stop allocating per flush,
  with no change to either file — they inherit it through `setInstances`.
- In this repo, `DrawRenderer` deletes `FrameBuffers`, `framesInFlight`,
  `frameIndex`, `ensureCapacity` and `uploadGeometry`'s growth handling — around
  forty lines — and the CLAUDE.md entry "Three rotating buffer sets" goes with
  them.
- It is on the critical path for the glTF work, where per-frame uniform and
  skinning data has the same shape.

**Deliberately out of scope: true suballocation.** The obvious further step is
one large arena with a bump pointer, handing out `(buffer, offset)` pairs. That
is better — one allocation instead of a pool, and fewer buffer binds — but it
requires `setVertexBuffer` and `drawIndexed` to take a byte offset, which the
API does not have. Revisit when draw-call overhead is actually measured to
matter, which is to say after §2.2 exists.

### 1.3 Packed vertex formats

**The gap.** `VertexFormat` offers four options:

```cpp
enum class VertexFormat { Float, Float2, Float3, Float4 };
```

There is no normalized-byte, half-float or normalized-short attribute.

**What this backend does about it.** `DrawVertex` unpacks `ImDrawVert`'s single
RGBA8 colour word into four floats. The colour attribute goes from 4 bytes to 16,
and the vertex from 20 bytes to 32 — 1.6× the vertex bandwidth for every ImGui
vertex in every frame. The unpack itself is free, riding along inside the copy
that rebases indices, so the cost is purely bandwidth and footprint.

**Why it matters far more for the engine.** Packed attributes are how mesh data
is normally stored, and how glTF ships it. A typical vertex is position
(3×float32) plus a packed normal, packed tangent and half-float UVs — around 24
bytes. Unpacked to float everywhere it is 48. Vertex fetch is a real cost in a
scene renderer in a way it is not in a UI.

**The change.** Extend the enum and map it in both backends:

| `VertexFormat` | Metal | D3D12 |
| --- | --- | --- |
| `UByte4Norm` | `MTLVertexFormatUChar4Normalized` | `DXGI_FORMAT_R8G8B8A8_UNORM` |
| `Half2` | `MTLVertexFormatHalf2` | `DXGI_FORMAT_R16G16_FLOAT` |
| `Half4` | `MTLVertexFormatHalf4` | `DXGI_FORMAT_R16G16B16A16_FLOAT` |
| `Short2Norm` | `MTLVertexFormatShort2Normalized` | `DXGI_FORMAT_R16G16_SNORM` |
| `Short4Norm` | `MTLVertexFormatShort4Normalized` | `DXGI_FORMAT_R16G16B16A16_SNORM` |

**The EDSL side is the interesting half.** `vertexInput(&Vertex::color)` deduces
the attribute format from the C++ member's type, so a packed member needs a way
to say "four bytes on the wire, `Float4` in the shader". The existing convention
already points at the answer — `Apps/GPU/Teapot`'s `Vec3` declares
`using ShaderValue = Float3;`. Mirror it with wrapper types:

```cpp
struct DrawVertex
{
    float position[2];
    float uv[2];
    GPU::UNorm8x4 color;   // 4 bytes on the wire, Float4 in define()
};
```

where `GPU::UNorm8x4` is a four-byte struct declaring
`using ShaderValue = Float4;` and its own `VertexFormat`. The shader body is
unchanged; only the storage is.

**Files.**

- `Lib/eacp/GPU/Pipeline/VertexLayout.h` — the enum entries and a
  `bytesPerAttribute` helper.
- `Lib/eacp/GPU/Pipeline/RenderPipeline-Apple.mm`,
  `RenderPipeline-Windows.cpp` — the two format mapping tables.
- `Lib/eacp/GPU/Codegen/ShaderValue.h`, `ShaderBuilder.h` — the wrapper types and
  the deduction that maps them to a shader value plus a wire format.

**Risk.** Higher than the other two, because it touches both backends' pipeline
creation, and a wrong mapping shows up as garbled geometry rather than a build
error. This is what the conformance test in §2.1 is for.

### 1.4 Smaller items already logged in this repo's README

Not part of this plan's critical path, but worth carrying in the same branch
series while the eacp worktree is open:

- `Graphics::MouseCursor` has no diagonal resize shapes, so ImGui's two corner
  grips come back as the arrow.
- `io.AddFocusEvent` needs *window activation*, which eacp does not expose; the
  view polls per-view `hasFocus()` instead.

---

## 2. Measurement

Two separate problems: proving a change is *correct* and proving it is *faster*.
eacp's test infrastructure covers the first well and does not address the second
at all.

### 2.1 Unit tests

**Where.** eacp's tree, `Tests/GPU/`, with new files added to the
`gpu_test_sources` list in `Tests/GPU/CMakeLists.txt`. They run from build tree
1, not from this repo.

**The idiom**, from the existing suite:

```cpp
auto tSomething = test("Group/caseName") = []
{
    auto image = renderDrawing([](auto& sprites) { /* ... */ });

    if (image.width() == 0)   // no GPU device: self-skip
        return;

    check(isGreen(image.at(image.width() / 2, image.height() / 2)));
};
```

Tests that need real pixels drive a `GPUView` subclass through
`View::renderToImage`, which runs the off-screen path with no window on screen —
so all of this is CI-able. `Tests/GPU/SpriteBatchTests.cpp` is the model to copy.

**`StreamingBufferTests.cpp`** — the properties that matter, stated as
assertions:

- *Recycling has the right period.* Record the address of the buffer returned by
  `write()` on each of ten consecutive frames; assert the sequence repeats with
  period `framesInFlight` and that no two consecutive frames share one. This is
  the actual correctness property — a buffer reused too early is the tearing
  bug — and it is checkable without a GPU.
- *Two writes in one frame get two buffers.* The `SpriteRenderer` case. Assert
  the returned references differ.
- *Steady state allocates nothing.* Write the same size for twenty frames;
  assert `bufferCount()` stops growing after the pools are warm.
- *Growth is geometric and never shrinks.* Write an increasing size, then a
  small one; assert `bufferCount()` did not rise on the small write.

**The allocation-count regression gate.** Add a counter to `Device`,
incremented in `Buffer`'s constructor and exposed as `Device::buffersCreated()`.
That turns "allocation churn" from a timing anecdote into a deterministic,
cross-platform integer, and it makes the `Sprites` fix directly testable:

```cpp
auto tSpriteFlushDoesNotAllocatePerFrame =
    test("SpriteBatch/steadyStateAllocatesNoBuffers") = []
{
    // Warm the pools, then assert ten more frames of identical drawing create
    // no further GPU buffers. Before StreamingBuffers this climbs by one per
    // flush per frame, forever.
};
```

This is the single most valuable test in the plan: it fails loudly on the exact
regression the work exists to prevent, needs no timing, and cannot flake.

**`BaseVertexTests.cpp`** — six vertices, an index buffer of `{0, 1, 2}`, drawn
twice with `baseVertex` 0 and 3. Assert the two triangles land in different
places by reading pixels back. Nothing else proves the parameter is threaded
through both backends correctly.

**`VertexFormatTests.cpp`** — the cross-backend conformance test, and the reason
§1.3 is the risky item:

- A quad whose colour attribute is `UNorm8x4{255, 0, 0, 255}` must produce the
  same pixels as one whose colour is `Float4{1, 0, 0, 1}`.
- A quad with `Half2` UVs sampling a 2×2 texture must produce the same pixels as
  one with `Float2` UVs.

Comparing two renders against each other rather than against a constant is
deliberate — the snapshot path returns premultiplied pixels, so absolute values
are a trap the README already warns about.

### 2.2 Performance measurement

Nothing exists today: NanoTest has no benchmark facility, and the GPU module has
no timestamp queries. Two layers, answering different questions.

**Layer 1 — CPU side. Available now, needs no GPU module feature.**

A new `Apps/Bench/` in *this* repo, which is the natural home: it is an ImGui app
measuring the ImGui backend, so the tool is drawn by the thing it measures.

- Drives `ImGuiView` in continuous mode against a controlled synthetic workload
  — sliders for window count, draw commands per window, vertices per command —
  so the numbers describe a fixed load rather than whatever the demo window
  happens to contain.
- Wraps `DrawRenderer::prepare()` and `encode()` in `std::chrono::steady_clock`
  and keeps the last few hundred frames in a ring, reporting p50 / p95 / max.
  Percentiles rather than a mean: the allocation cost this plan removes shows up
  as a tail, and a mean hides it.
- Plots `Device::buffersCreated()` per frame. This is the headline number and it
  should read zero in steady state once §1.2 lands.
- Reports vertex and index bytes per frame, which is what §1.1 and §1.3 move.

Record a baseline against eacp `main` *before* any of the work, and keep the
numbers in this file.

**Layer 2 — GPU side. A module feature, and a prerequisite for the engine work.**

Timestamp queries: `MTLCounterSampleBuffer` on Metal, an `ID3D12QueryHeap` of
`D3D12_QUERY_TYPE_TIMESTAMP` plus `GetTimestampFrequency` on D3D12.

~~A minimal API that fits the existing shapes: `Frame::mark(std::string_view)`,
recording a timestamp at that point in the command buffer.~~ **Not possible on
Metal.** `[MTLDevice supportsCounterSampling:]` on an M5 Max answers yes to
`AtStageBoundary` and no to all four of `AtDrawBoundary`, `AtBlitBoundary`,
`AtDispatchBoundary` and `AtTileDispatchBoundary` — so a timestamp can be taken
where a pass begins and ends, and nowhere else. There is no mechanism for a mark
in the middle of a frame to be built on.

So the unit is the pass, and a pass is timed by naming it:

```cpp
auto pass = frame.beginPass({.clearColor = c, .label = "ui"});
```

with `Device::lastFrameTimings()` handing back label → milliseconds plus the
frame end to end, and the `Bench` app rendering it as a flame strip. An
unlabelled pass is not timed and costs nothing, which is what keeps this off the
bill for an app that never looks.

**Scope.** This is Tier 3 work and must not block §1.1–§1.3, none of which need
it. But it should land *before* the glTF/renderer phase: past that point, every
optimisation decision without GPU timings is a guess.

### 2.3 What gets gated in CI

- The `Tests/GPU` additions run in eacp's existing CI with no new infrastructure.
- The allocation-count assertion is the meaningful gate — it is the one that
  catches a future change quietly reintroducing per-frame allocation.
- `Apps/Bench` is a developer tool, not a gate. Wall-clock thresholds in CI on
  shared runners flake; the counter assertions do not.

---

## 3. Order of work

**Phase 1 — base vertex (§1.1).** Smallest change in the plan, and the point of
doing it first is not the feature. It exercises the entire loop end to end —
worktree, branch, `CPM_EACP_SOURCE`, two build trees, a new `Tests/GPU` file, a
change in this repo consuming it — on a change whose blast radius is a defaulted
parameter. Any friction in §0 surfaces here, where nothing else is in flight.

Done when: `BaseVertexTests` passes on at least one backend, `DrawRenderer` no
longer rebases indices, and its index buffer is `ImDrawIdx`-width.

**Status: done on `gpu-base-vertex` (both repos), Metal verified.** The eacp
side is the two signatures plus four call sites; `ShaderProgram.h` needed no
change after all, because the `draw(Program&)` templates live in `RenderPass.h`
and the defaulted parameter covers them. `BaseVertexTests.cpp` has four cases
(plain and instanced, offset and zero) and they were confirmed to fail with the
Metal `baseVertex` pinned back to `0`, so they test what they claim to; the
whole suite is 141 passed, 0 failed. `DrawRenderer` now copies indices with one
`memcpy` and passes `base + VtxOffset` per draw; `Demo` renders identically.
D3D12 is written but unverified — no Windows machine in this loop.

**Phase 2 — streaming buffers (§1.2).** The highest-value item. Land
`Device::buffersCreated()` in the same branch, since it is how the change is
demonstrated.

Done when: `StreamingBufferTests` and the `Sprites` allocation assertion pass,
`DrawRenderer` has deleted its rotation, and `Bench` shows a flat buffer count.

**Status: done on `gpu-streaming-buffer` (both repos), Metal verified.** Suite
is 147 passed, 0 failed. Every recycling test was confirmed to fail with
`write()` reverted to allocating per call, including the `Sprites` gate, so they
catch the regression they exist for.

Two things came out differently from the plan above:

- **Off-screen frames bump the counter too**, which §4 guessed they should not.
  The reasoning there was right — `OffscreenTarget` blocks, so nothing is in
  flight — but it misses that *not* advancing makes a loop of off-screen renders
  one endless frame, on which the pool takes a fresh buffer every pass and never
  reclaims one. `renderToImage` in a test loop is exactly that shape.
- **`write()` copies the byte count it is given**, like `Buffer::update`, so a
  short source with a long count reads off the end. Cost an hour to a SIGBUS in
  a test that passed a 256-byte payload and claimed 4 MB.

`Bench` does not exist yet (Phase 4), so the flat-count evidence is the `Demo`
panel, which now reports `Device::buffersCreated()` and its per-frame delta: it
reads `6 GPU buffers created (+0 this frame)` — three vertex pools, three index
pools — and stays there.

**Phase 3 — packed vertex formats (§1.3).** Riskiest, and best done once the
conformance harness from Phase 1 exists to copy.

Done when: `VertexFormatTests` passes on both backends and `DrawVertex` is back
to 20 bytes.

**Status: done on `gpu-packed-formats` (both repos), Metal verified.** Suite is
153 passed, 0 failed. `DrawVertex` is 20 bytes, asserted at compile time.

The conformance test was checked against the mistake it exists for: with Metal's
`UByte4Norm` pointed at `MTLVertexFormatUChar4` (the plain integer variant
rather than the Normalized one) and `Half2` at `Float2`, the two comparison
cases fail. Both are silent errors — the pipeline still builds and the draw
still happens — which is why the file compares two renders rather than checking
colours against constants.

Three things worth carrying forward:

- **`toVertexFormat` had to become `constexpr`.** It was `inline`, and the
  format is now needed as a template constant rather than a runtime value.
- **A shader with no uniforms still needs `EACP_SHADER()`**, empty.
  `reflectMembers` is pure virtual, so a program without the macro is abstract
  and the error names the field rather than the cause.
- **Half needed a CPU-side conversion**, which the plan did not budget for:
  MSVC has no `_Float16`, so `halfFromFloat`/`halfToFloat` are written out in
  `PackedVertex.cpp` and tested directly, edges included.

The CPU wrapper types are named for what they store — `UNorm8x4`, `Float16x2`,
`Float16x4`, `SNorm16x2`, `SNorm16x4` — while the `VertexFormat` entries keep
the graphics-API spelling (`UByte4Norm`, `Half2`) that a Metal or D3D12 table
uses. `Half3` and a three-component normalized short are deliberately absent:
D3D12 has no 16-bit three-component vertex format, so either would work on
Metal and fail to build a pipeline on Windows.

**Phase 4 — the `Bench` app and timestamp queries (§2.2).** The plan folded
Layer 1 in alongside Phase 2, since it is what shows Phase 2 worked. That did
not happen: the `Demo` panel's `Device::buffersCreated()` readout answered the
one question Phase 2 had to answer, and building a whole app to ask it again
would have held the phase up for nothing.

So Layer 1 was still owed, and its remaining value was the part the Demo panel
does not cover — a *controlled* workload rather than whatever the demo window
happens to contain, and `prepare()`/`encode()` timings as percentiles rather
than a frame rate.

**Status: Layer 1 done on `gpu-packed-formats`, Metal verified. This repo only —
`Apps/Bench` needs nothing from eacp that Phases 1 to 3 did not already land.
Layer 2 not started, and unblocked.**

`Apps/Bench` drives `ImGuiView` against a load set by three sliders: draw lists,
commands per list, rects per command. A command is a clip rect, because a clip
rect change is what makes ImGui start a new `ImDrawCmd` — bands rather than
whatever a widget happens to emit is what makes the count exact. Every rect is 4
vertices and 6 indices whether or not it lands on screen, so the load is the
number the sliders say.

The one library change it needed is `DrawRenderer::getPrepareTime()` and
`getEncodeTime()`. The plan said the app would wrap the two calls itself, which
it cannot: `ImGuiView::render` is what calls them, and overriding `render` in the
app to get a clock around them forks the library's frame logic into the tool
measuring it. Two clock reads a frame, always on — see the header for why not
behind a switch.

Numbers, RelWithDebInfo, Apple silicon, 120Hz, at the default load of 8 lists ×
8 commands × 64 rects (so 64 draws and 16384 rect vertices asked for):

| | p50 | p95 | max |
| --- | --- | --- | --- |
| `prepare()` | 53.0µs | 95.8µs | 113.0µs |
| `encode()` | 21.2µs | 36.6µs | 56.2µs |

The frame that costs those is 21644 vertices, 32688 indices and 74 draws — the
~5000 vertices and 10 draws over what the sliders ask for are the panel drawing
itself, which is what "the tool is drawn by the thing it measures" costs. It
moves 487 KB of geometry a frame, and the allocation line reads `6 buffers
created (+0 this frame), worst frame in the last 512: +0`: three vertex pools,
three index pools, and nothing after that.

Three things worth carrying forward:

- **A Debug build measures something else, by 13×.** The same load reads
  `prepare` p50 716µs there against Release's 53µs, while `encode` reads 22µs
  against 21.2µs. Encode is API calls, which the optimiser cannot do anything
  about; prepare is the per-vertex copy, which it can. A Debug number says
  nothing at all about §1.3.
- **The missing baseline is arithmetic rather than a measurement.** No run
  against pre-Phase-1 eacp was ever recorded and checking it back out for one is
  not worth the afternoon. The same frame at the 32-byte vertex and 32-bit
  indices this started from is 21644 × 32 + 32688 × 4 = 804 KB, against the
  487 KB it moves now — 1.65×, which is what §1.1 and §1.3 said it would be.
- **The allocation counter needed a window, not a readout.** An allocation is a
  single-frame event, so a live "+N this frame" shows it for one 120th of a
  second and then forgets. The ring keeps the worst frame in the last 512, which
  is the form the number is actually usable in.

**Status of Layer 2: done on `gpu-timestamps` (both repos), Metal verified.**
The suite is 159 passed, 0 failed, up from 153.

What shipped is per-pass timing rather than the `mark()` in §2.2, for the
hardware reason recorded there. The shape:

- `Timing/FrameTimings.h` — `PassTiming` and `FrameTimings`, the results.
- `Timing/GpuTimestamps.h` + two backend files — the seam. This is where the
  backends stop resembling each other, and drawing the line here is what keeps
  the ring of frame slots portable.
- `Timing/FrameTimer.{h,cpp}` — that ring. A frame writes into the slot for
  `frameIndex % 4` and a slot is read only once the backend says the GPU has
  finished with it, so nothing anywhere waits.
- `Device::beginFrame()` drives it, exactly as it drives `StreamingBuffers` —
  one advance, nothing for an app to call, nothing to forget.

`ImGuiView` gained `ViewOptions::passLabel`, empty by default: two counter
samples a pass is not much, but an app that never reads the numbers should not
be paying for them.

Numbers from `Bench`, same default load as Layer 1: the GPU frame is **238µs
p50 / 284µs p95**, of which the `imgui` pass is all of it — there is only the
one pass — against 53µs of `prepare` and 21µs of `encode` on the CPU. So this
load is nowhere near either limit at 120Hz, which is the first thing the two
layers together are able to say.

Three things worth carrying forward:

- **Apple silicon samples at stage boundaries and nowhere else.** Probed rather
  than assumed, and it is the fact the whole API shape rests on. Worth
  re-probing on any new hardware before assuming a mark can be put anywhere.
- **`sampleTimestamps` hands back nanoseconds, not `mach_absolute_time` ticks.**
  Multiplying by the mach timebase — 125/3, or 41.667ns a tick on Apple silicon
  — put every pass out by that factor, and the first version of the bounds test
  had a one-millisecond tolerance that waved it straight through: two passes
  reading 0.19ms each sat inside a frame reading 0.02ms. Two numbers from
  different clocks need a check tight enough to notice they disagree, which is
  now `sum(passes) <= frame + 0.05ms`.
- **A new field on `RenderPassDescriptor` needs its own initialiser.** Without
  `= {}` on the new `label`, every `beginPass({colour})` already in eacp warns
  under `-Wmissing-field-initializers`. Three unrelated files turned it up
  immediately, which is the warning doing its job.
- **A device with no counters wedged the timer, and CI found it.** A slot the
  timer waits on has to be answerable or it is never recycled, and four of those
  is the timer off for the rest of the process. That is what an unsupported
  device got: `endSlot` returned early without keeping the command buffer, so
  nothing ever completed — including the frame total that needs no counters and
  was documented as still working. `endSlot` now reports whether the slot will
  have an answer and the timer only leaves it pending if it will.

  The point worth carrying: this failed on *macOS*, the platform it was
  developed on, because GitHub's runners are paravirtualised and this machine is
  not. A capability check with a fallback path has a second configuration in it
  that the developer's hardware may be unable to enter, and CI is where it gets
  entered. It reproduces locally by forcing `supported` false, which is how the
  fix was checked both ways.

**Then** the glTF corpus, `cgltf`, and the Tier 1 renderer gaps — mips, face
culling, depth compare/write control, viewport — with this backend's ImGui
overlay as the inspector, which by then is free.

---

## 4. Notes and open questions

- **Where does `StreamingBuffers` get its frame depth?** The plan hardcodes 3,
  matching `DrawRenderer`'s existing constant and Metal's default drawable pool.
  But `GPUView::setFramesInFlight` is per-view and can be 2 on Windows, so a
  device-wide constant is an over-estimate rather than a bug — it costs one
  extra pool. Revisit only if the memory shows up.
- ~~**Does `Device` already have somewhere sensible for a frame counter?**~~
  Answered in Phase 2: yes, and *both* `Frame` constructors bump it, including
  the off-screen one — not for correctness but because a loop of off-screen
  renders would otherwise be a single endless frame to `StreamingBuffers`. A
  `CommandBuffer` submitted outside a `Frame` still does not bump, and streaming
  is not the right type for data written on that path.
- **`setInstances` currently has an implicit contract** that each call gets
  independent storage. Writing that down in the header is part of Phase 2, since
  after the change it becomes a property of `StreamingBuffers` rather than an
  accident of allocating every time.
