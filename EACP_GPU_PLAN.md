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

Everything in this plan except the glTF corpus is on `main` in both
repositories. §3 carries what each phase actually cost and what the plan got
wrong about it.

| Phase | Branch | State |
| --- | --- | --- |
| 1 — base vertex (§1.1) | `gpu-base-vertex` | Done, both backends, merged |
| 2 — streaming buffers (§1.2) | `gpu-streaming-buffer` | Done, both backends, merged |
| 3 — packed vertex formats (§1.3) | `gpu-packed-formats` | Done, both backends, merged |
| 4a — `Bench`, CPU side (§2.2) | `gpu-packed-formats` | Done, this repo only, merged |
| 4b — timestamp queries (§2.2) | `gpu-timestamps` | Done, both backends, merged |
| 5 — culling + depth state (§3) | `gpu-pipeline-state` | Done, both backends, merged |
| 6 — viewport (§3) | `gpu-viewport` | Done, both backends, merged |
| 7 — mipmaps (§3) | `gpu-mipmaps` | Done, both backends, merged |
| 8 — glTF (§5) | `mesh-gltf` | First slice done, both backends, unmerged |

Phases 1 to 7 were stacked, each cut from the one before, so the tip contained
all of them and landing the lot was one fast-forward per repository. Every name
among them is now strictly inside `main` and can be deleted. Phase 8 is cut from
`main` in each repository rather than continuing the stack, because it is the
first phase that adds a module rather than closing a gap in one.

The merge order is eacp first, always. `CMake/FindEACP.cmake` fetches
`GIT_TAG main`, so between Phase 3 landing here and eacp's side reaching eacp
`main`, this repository's CI was red on a structural rather than a real failure:

```
DrawRenderer.h(21): error C2039: 'UNorm8x4': is not a member of 'eacp::GPU'
```

Worth remembering rather than rediscovering — while a phase is in flight here,
this repository's CI says nothing at all.

When Phase 7 landed, eacp's `GPUTests` was **191 passed, 0 failed**, up from 141
before any of this, and the whole suite was **834 tests** in CI where it was 794.
Phase 8 adds 38 more, all of them in `Tests/Mesh` and none in `GPUTests`.
Every new assertion was checked against the failure it exists for by breaking
the thing deliberately and watching it fail — the base vertex pinned back to
zero, `StreamingBuffers::write` reverted to allocating per call, Metal's
`UByte4Norm` pointed at the non-normalized format, the GPU tick scale put out by
the mach timebase, a pass's two sample indices swapped, a cull mode that never
reached the encoder. None of them is a build error, and none shows up as
anything but wrong pixels, a rising counter or a number that is quietly the
wrong size.

`DrawRenderer` is the visible result in this repo: 16-bit indices copied with
one `memcpy`, no buffer rotation of its own, and a 20-byte vertex — against
32-bit rebased indices, three hand-rolled buffer sets and a 32-byte vertex when
this document was written.

~~**Everything is Metal-only verified.**~~ **The D3D12 side runs, and is
tested.** Every phase above was written blind for D3D12 and described here as
unverified, on the assumption that a Windows machine was needed to say
otherwise. It was not: eacp's CI runners have a working D3D12 device, so
`Tests/GPU` does not self-skip there — it *renders and reads pixels back*.
`gpu-pipeline-state` is green at **803/803 on Windows MSVC, Windows Clang,
Windows MSVC ARM64 and Windows Clang ARM64**, `BaseVertex`, `VertexFormat`,
`FrameTiming` and `PipelineState` among them.

So the two `RenderPipeline` mapping tables in §1.3 — the part of this plan most
likely to be silently wrong, since a bad entry is not a build error on either
side — have been checked by the conformance test on the backend they were
written blind for. Worth knowing for the phases after this one: pushing a branch
*is* the Windows check, and waiting for hardware to do it was never necessary.
Phase 5 was written that way deliberately and settled a question — whether the
two APIs call the same triangle front-facing — that no single machine could
have answered.

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

**One of those four examples turned out to be wrong, and it is worth knowing
which.** Phase 8 measured the half-float UV and gave it back: half's precision is
relative, a texel's is not, and a tiled UV is out by sixteen texels of a 1024
texture. See §5.4. The packed *normal* and the packed colour hold up exactly as
argued, and the difference between the two cases is the rule this section should
have stated: a fixed-width format works where the range is known in advance,
which is true of a direction and a colour and false of a texture coordinate.

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

**Phase 5 — pipeline state a mesh renderer needs.** The first two of the Tier 1
gaps, and the first phase with nothing in this repository: ImGui is 2D, draws no
depth and culls nothing, so there is no consumer here. It is groundwork for the
glTF work, which §2.2 said this point in the plan was for.

Done when: a pipeline can say which faces to throw away and what its depth test
does, and the two backends agree about both.

**Status: done on `gpu-pipeline-state` (eacp only), verified on both backends.**
The suite is 168 passed, 0 failed locally and **803 on all four Windows
toolchains**, up from 159 / 794.

`RenderPipelineDescriptor` gained four fields, every one defaulted to what was
previously hardcoded, so nothing already drawing changes:

```cpp
CullMode     cullMode     = CullMode::None;
Winding      frontFace    = Winding::Clockwise;
DepthCompare depthCompare = DepthCompare::LessEqual;
bool         depthWrite   = true;
```

`depth` keeps its old meaning — whether there *is* a depth attachment, which the
pass and the pipeline must agree on or both backends reject the draw. Splitting
`depthWrite` off the comparison is the point of the exercise: translucent
geometry tests against the opaque depth already written and must not write its
own, or the nearer of two glass surfaces hides the further one instead of
blending over it. One `bool depth` could not express that at all.

`ShaderProgram::prepare` gained a descriptor overload rather than two more
defaulted parameters — five positional arguments was already where a call site
stopped being readable, and every future setting would have had to be repeated
in that signature.

Four things worth carrying forward:

- **Metal and D3D12 agree on which triangle is front-facing.** Both decide
  facing after the viewport transform, so `Winding::Clockwise` means clockwise
  on the rendered image on either backend. `PipelineStateTests` asserts that
  *absolutely* rather than only that the two vertex orderings differ — a
  relative assertion passes on both backends even when they disagree, and
  portable culling was the whole point. CI is what settled it.
- **Culling is the one setting where the APIs disagree about *when* state is
  fixed**, not about what it is called: D3D12 bakes it into the pipeline object,
  Metal sets it on the encoder. Resolved the way `topology` already was. The
  trap is applying it only when it culls something — then a pass drawing a
  culled mesh and then a full-screen quad loses half the quad, on Metal only.
  There is a case for exactly that.
- **A case that passes for one reason is worth as much as one that fails.** Each
  of the nine is a *pair* differing in one field with opposite expected results.
  That shape is what catches a field ignored outright, since ignoring it makes
  both halves agree — a single-outcome case cannot tell "this field works" from
  "something here draws green". Two drafts had both halves expecting the same
  colour and were rewritten.
- **Every assertion was checked against the failure it exists for**, as in every
  phase before it: the cull mode never reaching the encoder, the winding pinned
  to its default, the cull mode applied conditionally, `depthWriteEnabled`
  pinned on, the comparison pinned to less-equal. Each break was caught by
  precisely the cases naming it and by no others.

Also worth noting: this was the first test to exercise D3D12's off-screen depth
attachment at all. It was written and correct — but nothing had ever run it.

**And then it collided.** While this branch was in flight, face culling was
implemented independently on eacp `main` — `CullMode`, a `cullMode()` accessor,
`CullModeTests.cpp`, the lot. Two implementations of one feature, arrived at
separately, touching all ten of the same files. That is recorded here because
the resolution is the interesting part and because nothing in §0 anticipated it:
the plan assumed this branch series was the only thing moving in eacp, and it
was not.

It was merged rather than rebased, `main`'s behaviour kept as the default
wherever the two disagreed:

- **`CullMode` is `main`'s** — enum order, comments, tests. `Winding` survived
  from this branch as a *field* rather than a fixed rule, defaulting to
  `CounterClockwise` so a pipeline that says nothing gets exactly what `main`
  did. It earns its place on the geometry that does not arrive in glTF's
  winding: a mesh wound the other way, an instance mirrored by a negative scale,
  an inside-out skybox — none of which should need its indices rewritten.
- **`main`'s backend comments replaced this branch's, and that is a correction.**
  This branch reasoned that both APIs decide facing after the viewport transform
  and wrote that down. `main` *measured* it — found `FrontCounterClockwise =
  FALSE` culled the opposite face on D3D12 — and recorded that the two APIs
  spell one convention identically because clip-space y is up and the
  framebuffer origin is top left on each. Same conclusion, arrived at the better
  way, and its comment explicitly warns off the reasoning used here.
- **`prepare` is `main`'s.** Both sides added the same descriptor overload;
  `main`'s positional form already builds a descriptor and delegates, which is
  what this branch was reaching for, so the duplicate went.

The two test files now overlap deliberately: `PipelineStateTests` sets
`frontFace` explicitly on every step and so tests the *field*, while
`CullModeTests` owns the *default convention*. Neither needed changing to
coexist.

**Phase 6 — viewport.** `RenderPass::setViewport` / `clearViewport`, alongside
the scissor calls and in the same units.

Done when: clip space can be mapped onto part of the target rather than all of
it, and the two backends agree about where.

**Status: done on `gpu-viewport`, verified on both backends.** 175 passed, 0
failed locally; **810/810 on all four Windows toolchains**.

Three things worth carrying forward:

- **A viewport is not a scissor, and a careless test cannot tell.** A
  full-screen quad drawn through a half-target viewport and the same quad
  through a half-target scissor produce the *same picture*. So the geometry in
  `ViewportTests` covers half of clip space rather than filling it: a viewport
  moves it into the target's third quarter and a scissor would have deleted it.
  Any case built on full-screen geometry proves nothing about which one was
  implemented.
- **An out-of-target rect is refused, not clamped**, and the codebase had
  already answered this once: `Texture::update(region, ...)` refuses a region
  that is not wholly inside the texture, explicitly *not* clamping, because a
  clamped region uploads skewed pixels that are harder to spot than nothing
  appearing. Same logic — a clamped scissor still shows what the caller asked
  for, but a clamped viewport keeps drawing and squashes the picture into a
  rectangle nobody chose.
- **Neither backend forces that refusal.** With the bounds check removed, Metal
  took the out-of-target viewport without complaint and drew through it. It is
  eacp's policy, and `ViewportTests` is the only thing holding the two backends
  to it.

**Phase 7 — mipmaps.** `TextureDescriptor::mipmapped`, the chain, and the
sampler that reads it.

Done when: a minified texture stops aliasing, and both backends produce the same
pixels doing it.

**Status: done on `gpu-mipmaps`, verified on both backends.** 191 passed, 0
failed locally; **834/834 across the Windows toolchains**, the per-subresource
upload and the re-upload path among them.

Four things worth carrying forward:

- **The chain is built on the CPU, for both backends, deliberately.** Metal has
  `generateMipmapsForTexture`; D3D12 has no equivalent — a chain there is a
  compute shader, a UAV per level and a root signature to bind them. So the
  choice was a GPU chain on one backend against a hand-written one on the other,
  which is two filters producing two pictures for the same texture and the one
  thing no conformance test could check, or one filter producing the same bytes
  for both. It costs 4/3 of the pixels moved, once, at creation.
- **It found a divergence that predated all of this.** D3D12's static samplers
  had declared `MIN_MAG_MIP_LINEAR` since they were written; Metal's left
  `mipFilter` at its default of `NotMipmapped` — "sample level 0, whatever
  levels exist". The backends had disagreed from the start and nothing could
  see it, because no texture had a second level. The first mipmapped one would
  have been filtered on Windows and read at full size on Apple, from the same
  `TextureSampling`, with no symptom but the picture.
- **The scope estimate was wrong, and in the useful direction.** This was
  expected to double the sampling configurations from four to eight and touch
  the D3D12 root signature. It does neither: mip filtering on a single-level
  texture is what both APIs do anyway, so it is invisible to every texture
  without a chain.
- **A red/green checkerboard sampled `Nearest` is what makes the drawing case
  decisive.** `Linear` averages a 2×2 neighbourhood of level 0, and a 2×2
  neighbourhood of a checkerboard is two red texels and two green — the same
  blend a mip level holds, so the case would pass with no mips at all.

And one lesson that is not about the GPU at all:

- **eacp builds as a unity build in CI and this plan tells you to configure it
  off.** `MipChain.cpp` declared `Block` in an anonymous namespace and
  `ShaderGraph.cpp` already had one; an anonymous namespace does not separate
  two files that a unity build concatenates into a single translation unit, so
  every use of either became ambiguous. The collision *does not exist* in the
  `-DEACP_UNITY_BUILD=OFF` tree §0.3 asks for. `EACP_CI_BUILD=ON` mirrors CI's
  flags exactly, and a second build tree configured with it catches this class
  before a push — which is the same shape as the counter-less GPU in Phase 4b:
  a configuration the developer machine does not enter by default.

**Phase 8 — the glTF corpus**, using this backend's ImGui overlay as the
inspector, which by now is free. Everything §2.2 wanted in place first is:
GPU timings to make optimisation decisions with, and the full Tier 1 set — mips,
face culling, depth compare and write control, viewport — to draw with. It is
the first phase big enough to need a design rather than a paragraph, so it has
its own section: **§5**.

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

---

## 5. Phase 8 — the glTF corpus

The first phase that adds a module rather than closing a gap in one, and the
first whose value is not a feature the GPU module was missing. Phases 1 to 7
were each argued for by pointing at a mesh renderer that did not exist yet. This
is that renderer, and it is therefore also the audit: every one of those seven
either gets used here or was justified by something that never arrived.

### 5.1 Which repository, and why it is not this one

**eacp gets the loader and the renderer; imgui-eacp gets the inspector.**

This is the same argument §1 opens with — the three gaps were moved into
`eacp-gpu` rather than worked around here precisely so that "`Sprites`, `Text`
and any future mesh renderer get them too". A mesh renderer that lives in the
ImGui backend is not a mesh renderer the rest of eacp can use, and putting it
here would make this repository the home of an engine feature that has nothing
to do with ImGui.

What genuinely belongs here is the **inspector**: a node tree, per-primitive
counts, a material list and the pass timings, all of which want an immediate-mode
UI and none of which want to be in eacp. So the split falls where the ImGui
dependency does.

| | Repository | Target |
| --- | --- | --- |
| glTF parse, CPU scene, GPU upload, the draw | eacp | `eacp-mesh` |
| Orbit camera, node/material inspector, timings panel | imgui-eacp | `Apps/Model` |

Branch `mesh-gltf` in both, cut from `main` in each rather than continuing the
Phase 1–7 stack, and merged eacp-first for the reason §0 gives.

### 5.2 The parser, and why it is ours

glTF is JSON plus a binary blob, and the obvious move is to take `cgltf` — a
single MIT header, the de-facto standard C reader, no transitive packages. That
is what the first draft of this section did, and it was wrong for a reason it
never weighed: it would have been the first dependency in eacp that somebody else
wrote.

```
eyalamirmusic/NanoTest              eyalamirmusic/Miro
eyalamirmusic/cpp_data_structures   eyalamirmusic/ResEmbed
```

Every CPM package eacp has is its author's own, as are its JSON parser, its test
framework, its containers and its reflection. That is a posture, not a
coincidence, and a glTF loader is not the thing to break it for. **So the loader
is written here, on `Miro::Json`** — which reaches `eacp-mesh` transitively,
`Miro` being `PUBLIC` on `eacp-core`, and therefore costs nothing to adopt.

**What that is not is a 7,000-line saving**, and the measurement is worth keeping
because it is the argument anyone will re-make later:

| | lines |
| --- | --- |
| `cgltf.h`, total | 7,175 |
| — jsmn, its bundled JSON tokenizer | 373 |
| — cgltf's own code | 6,802 |
| &nbsp;&nbsp;&nbsp;&nbsp;of which: JSON → struct mapping for the *whole* spec | 3,534 |

`Miro::Json` replaces the 373. The other 6,802 lines are glTF semantics, and most
of them cover animation, skins, cameras, lights and some twenty `KHR_*`
extensions that §5.8 puts out of scope. So the real comparison was never 7,175
against a dependency — it was the subset this module reads, estimated at ~650
lines.

**It came to rather more than that.** Two files, and the estimate missed by about
a third:

| | estimate | actual (code lines) |
| --- | --- | --- |
| `GltfReader.{h,cpp}` — container, buffers, accessors | ~400 | 659 |
| `GltfLoader.cpp` — the schema mapping into `MeshData` | ~250 | 379 |
| `GltfReaderTests.cpp` — the paths cgltf used to cover | not budgeted | 550 |

Where the estimate went wrong is instructive: it costed the *happy path*. What it
left out is that a model file is untrusted input, so every read is bounds-checked
and every failure is a return value rather than an exception — and that
`Miro::Json`'s own accessors throw on a type mismatch (`std::get` on the variant,
`.at()` on the object), so none of them can be called directly. The safe readers
that wrap them, and the per-element bounds checks, are most of the overrun.

**What the split is.** `GltfReader` is the layer that used to be somebody else's:
the GLB container, buffer resolution, and the accessor indirection — five
component types, normalization, an optional byte stride, sparse overrides.
`GltfLoader` is the schema mapping on top, and `MeshData` above that has never
heard of either. The JSON is genuinely the small part; the layer between "the file
says accessor 3" and "here are 72 floats" is where the work is, and where the
bugs would have been.

**What is deliberately refused rather than half-implemented.** Matrix accessor
types: byte- and short-component matrices pad every column to four bytes, a rule
that is invisible until it skews a mesh. No attribute this reader consumes is a
matrix, so `componentsForType` returns zero for `MAT2`/`MAT3`/`MAT4` and the
accessor is refused. Same for a percent-encoded (rather than base64) data URI,
and for glTF 1.0, which is a different format behind the same extension.


### 5.3 The module

```
Lib/eacp/Mesh/
  MeshTypes.h      Vec3, Mat4, and the packed vertex
  MeshData.h       The CPU scene: nodes, primitives, materials, images
  GltfReader.{h,cpp}   Bytes -> numbers: GLB container, buffers, accessors
  GltfLoader.{h,cpp}   Numbers -> MeshData: the schema mapping
  MeshRenderer.{h,cpp} MeshData -> GPU buffers, textures, draws
  MeshShader.h     The shader, in the EDSL, like every other one
  Mesh.h           Umbrella header
```

`MeshData` is deliberately **format-agnostic** — it names nothing glTF calls
things. A loader is a translation into it, so a second one (OBJ, or a packed
runtime format) is a new file rather than a refactor, and `MeshRenderer` never
learns what produced its input. The seam is also what makes the loader testable
without a GPU, which is most of §5.7.

`GltfReader` is a second seam one level down, and it earns its place for the same
reason: the container and the accessor indirection are the parts with no opinion
about what a mesh is, and keeping them apart from the schema mapping is what lets
§5.7 test "does a byte stride work" without a node tree around it. It is
deliberately **not** in `Mesh.h` — it is an implementation detail of the loader,
and the tests that touch it include it directly.

**A CPU `Mat4` has to be written**, and that is worth flagging because eacp does
not have one. The matrix helpers are all EDSL-side — `perspective`, `rotateX`,
`translate` build a `Float4x4` *inside* `define()` from scalar uniforms, which is
what `Apps/GPU/Teapot` does and is exactly right for one spinning object. It
cannot work here: a glTF node's transform is composed down the hierarchy from
its parents, and the tree is data, not something a shader body can be written
against. So the matrices are built on the CPU and uploaded as `Float4x4`
uniforms, which `CpuValueOf<Float4x4>` already maps to an `Array<float, 16>`.

It goes in the Mesh module rather than in `Core` or `SIMD` for now, on the
grounds that a type with one consumer is not yet a shared abstraction. It must
match the EDSL's stated convention exactly — **column-major, right-handed,
`[0, 1]` depth** — because the two are multiplied together in the shader and
nothing catches a mismatch but the picture.

### 5.4 The vertex, which is what Phase 3 was for

```cpp
struct MeshVertex
{
    float position[3];        // 12 — a world position needs the mantissa
    GPU::SNorm16x4 normal;    //  8 — a direction, and the range is known
    float uv[2];              //  8 — see below; this was a Float16x2 and was wrong
    GPU::UNorm8x4 color;      //  4 — what COLOR_0 already is
};                            // 32 bytes, against 48 unpacked
```

**The UV was `Float16x2` and it is the one packed format this phase had to give
back.** §5.9 asked whether half had the precision for a tiled UV and named it the
most likely thing here to be wrong. It was, and the reason generalises past this
struct: **half's precision is relative and a texture's is not.** Eleven bits of
mantissa buy the same *proportional* accuracy wherever the value sits, while the
texel a UV has to resolve is 1/1024 of a tile whether the coordinate reads 0.5 or
40.5. So the error doubles every octave against a requirement that does not move:

| UV | worst error | texels of a 1024 texture |
| --- | --- | --- |
| 0 – 1 | 1/4096 tile | 0.25 |
| 4 – 8 | 1/512 tile | 2 |
| 32 – 64 | 1/64 tile | 16 |

Sixteen texels is a visible seam between two triangles that should meet, and a
UV of 40 is what an architectural floor or a terrain patch ordinarily arrives
with. The other packed attributes are not exposed to this, and the distinction is
the useful part: a normal is a direction and a colour is `[0, 1]`, so both have a
range known in advance, which is exactly what lets a fixed-point format spend its
whole precision on it. **A UV has no such range**, and that — rather than
anything about UVs specifically — is what makes it the attribute a packed format
gets wrong.

The vertex costs four more bytes for it, 32 against 28, and 32 divides a cache
line evenly where 28 did not. `MeshVertexTests` pins the numbers so that
repacking it fails a test rather than shipping a seam.

The remaining packed formats are `SNorm16x4` and `UNorm8x4`. The obvious further
step on the normal is an **octahedral** pair, which is two components rather than
four and would take the vertex to 28. It is deliberately not in this phase:
octahedral encoding is a decode in the shader — the one place §1.3 promised there
would not be one, because "both backends widen the attribute during the vertex
fetch, in hardware". A four-component normal with an unused `w` costs four bytes
and keeps that promise; it is the right trade until vertex fetch is measured to
be the limit, and `Bench` plus the Phase 4b pass timings are what would measure
it.

`Float16x2` and `Float16x4` are now both packed formats with no consumer here,
and that is a finding rather than a gap. `Float16x4` is the tangent's format and
tangents arrive with normal mapping, which §5.8 puts out of scope. `Float16x2`
lost its only consumer to the measurement above — which is worth carrying back to
§1.3, since "half-float UVs" is the example that section used to argue for the
format in the first place. Half is right for a bounded quantity that is not
addressing a texture; it is not right for a UV.

### 5.5 The draw, which is what Phases 1, 5 and 7 were for

A glTF file is a scene of nodes, each optionally holding a mesh, each mesh a list
of primitives, each primitive its own index range and material. That is exactly
the shape §1.1 described when it argued for a base vertex:

> Packing many submeshes into one vertex buffer and drawing each with its own
> base vertex is the standard mechanism for a mesh renderer, and glTF scenes
> arrive in exactly that shape.

So the whole scene uploads into **one vertex buffer and one index buffer**, once,
at load. Each primitive keeps its first index, its index count and its base
vertex, and every primitive's indices start from zero — which is what keeps them
16-bit on a scene far past 65536 vertices, the same property `DrawRenderer` gets.
These are static `GPU::Buffer`s, not `StreamingBuffers`: the geometry is written
once and never rewritten, which is the case `Buffer::update` was always fine for.

Per draw, the model matrix and the material factors change, and those go through
`RenderPass::setUniforms` — `setVertexBytes` on Metal, a transient constant
buffer on D3D12 — which is the path built for data that changes per draw. The
hand-rolled shape is `DrawRenderer::encode`'s, and the reason for hand-rolling
rather than `pass.draw(program)` is the same: one program, many draws.

The pipeline state is Phase 5's, and this is its first consumer:

```cpp
descriptor.depth        = true;
descriptor.cullMode     = CullMode::Back;
descriptor.frontFace    = Winding::CounterClockwise;   // the default, and glTF's
descriptor.depthCompare = DepthCompare::LessEqual;
```

`frontFace` needs no setting, which is the point — Phase 5 chose glTF's
convention as the default precisely so that this line does not have to exist.
The one case that does need it is a primitive whose node transform has a negative
determinant: a mirrored instance reverses the winding of every triangle under it,
and the fix is the flipped `frontFace` rather than rewriting its indices, which
is the case §5's Phase 5 notes said the field earns its place on. Whether the
corpus contains one is an open question in §5.9.

Materials with `doubleSided` get `CullMode::None`, so a pipeline is picked per
material rather than built once. Alpha-blended materials get
`BlendMode::AlphaBlend` and `depthWrite = false`, which is the exact case §1's
Phase 5 entry used to argue that one `bool depth` could not express what a
renderer needs — so it, too, gets its first real consumer here rather than only
a test.

Textures are created `mipmapped = true`, which is Phase 7 and is the difference
between a model that shimmers as the camera moves and one that does not.

### 5.6 The shader

One `MeshShader` in the EDSL, with a shading mode the inspector can switch:
base colour (the material factor times the base colour texture times `COLOR_0`),
normals-as-colour, and a single-light Lambert term.

Two of those three exist for the inspector rather than for the picture. A model
drawn flat-unlit is a silhouette, and a normal that failed to load, arrived
unnormalized, or was wrecked by the `SNorm16x4` packing looks *exactly* the same
as one that worked — so a mode that draws the normal directly is what makes the
packing in §5.4 falsifiable by looking. That is the same instinct as §2.1's
"comparing two renders against each other rather than against a constant": a
picture that can only look right is not evidence.

### 5.7 Tests

A new `Tests/Mesh/` in eacp, added to `Tests/CMakeLists.txt` under the same
platform gate `GPU` and `Text` sit behind.

Most of what can go wrong here needs no GPU, which is the payoff for the
`MeshData` seam:

- **Node transforms compose down the tree.** A child of a translated,
  rotated parent lands where the product says and not where either factor alone
  does. The single most likely thing to be silently wrong, because a
  wrong-but-plausible matrix convention still draws *something*.
- **TRS and a supplied matrix agree.** glTF lets a node give either; a node
  written both ways must produce the same world transform.
- **Primitives keep zero-based indices and get a base vertex.** Two primitives
  packed into one buffer, asserting the second's `baseVertex` is the first's
  vertex count and that its index values start from zero — the property that
  keeps them 16-bit, and one that a loader "helpfully" rebasing would break with
  no visible symptom until a scene passes 65536 vertices.
- **Material defaults are glTF's**, not zero-initialised C++ ones. A missing
  `baseColorFactor` is opaque white; a missing material is the default material.
  Getting this wrong renders a black scene, which reads as a lighting bug.
- **The packed vertex round-trips.** A normal and a UV through `SNorm16x4` /
  `Float16x2` and back, to the precision each format actually has.

Plus one rendered case through `View::renderToImage`, in the shape §2.1
established: two cubes at different depths, asserting the near one occludes the
far one — which fails if the depth state, the projection's handedness, or the
node composition is wrong, and is the cheapest single check that the whole chain
is connected.

Every assertion gets checked against the failure it exists for by breaking the
thing deliberately, as in all seven phases before it. That is the convention that
has caught something every time.

### 5.8 Deliberately out of scope

The first slice is **static, unlit-to-Lambert, single-scene**. Named here so the
absences read as decisions:

- **PBR.** Metallic-roughness, normal mapping, IBL. This is where it ends up, and
  it needs nothing new from the GPU module — which is exactly why it goes second.
  The parts that *prove* Phases 1 to 7 are the parts in this slice.
- **Animation and skinning.** Skinning is the one that would come back to the GPU
  module, and §1.2 already noted joint matrices have the same shape as per-frame
  uniform data. It wants `StreamingBuffers` and possibly a storage-buffer path.
- **Draco, KTX2, and the `KHR_*` extension set.** Each is a second dependency.
- **Sorting, frustum culling, instancing.** Scene-graph work rather than loader
  or backend work, and premature before `Bench` says the draw count matters.

### 5.9 Open questions

- **Does the corpus contain a mirrored node?** §5.5 says a negative-determinant
  transform is what `frontFace` exists for. If nothing in the test set has one,
  the path is written blind and should be tested with a deliberately mirrored
  node rather than assumed.
- **Where do the model files come from, and do they go in the repository?**
  Khronos's sample set is the obvious corpus and is far too large to vendor. A
  handful of small `.glb` files for the tests, authored in the test itself where
  possible, is the shape that keeps CI honest — the loader tests above are all
  written against glTF constructed in the test rather than a file on disk, for
  that reason. The app takes a path.
- ~~**Does `Float16x2` have the precision for a tiled UV?**~~ **Answered: no, and
  the UV is now a `Float2`** — which is the answer this question predicted, for
  very nearly the reason it gave. It was off by one step: the worry was that a UV
  of 40 keeps "a thousandth of *that*", but half's mantissa is relative, so the
  error is a thousandth of 40 rather than a thousandth of a thousandth. That is
  1/64 of a tile, or sixteen texels of a 1024 texture — bad enough to see and not
  as bad as feared. It did not need the corpus to show it: the arithmetic is the
  measurement, and `MeshVertexTests` states it per octave. See §5.4.
- ~~**Should the glTF parser be a dependency?**~~ Answered, and the answer went
  the other way from the first draft: no. See §5.2. eacp takes no third-party
  package for this, and cgltf never reached `main` in either repository.
- **How much of the spec does the reader still not read?** Known and listed in
  §5.8, but worth restating as a question because it is now *ours* to answer
  rather than a library's: no animation, no skins, no Draco, no KTX2, no `KHR_*`.
  Each is now a change to `GltfReader` instead of a version bump, which is the
  cost side of owning it. The reader refuses what it does not handle rather than
  reading it wrongly, which is what makes that cost bounded.

### 5.10 What the first slice cost, and what this section got wrong

**Status: done on `mesh-gltf` in both repositories, verified on both backends,
not yet merged.** eacp's suite is **38 new tests in `Tests/Mesh`**, and the Mesh
module added nothing to `GPUTests` — it needed nothing from the GPU one that
Phases 1 to 7 had not already landed, which is the strongest thing this phase
says about them. The module builds clean under `EACP_CI_BUILD=ON` as well, which
is the unity-build trap Phase 7 recorded, and takes **no third-party
dependency** — see §5.2 for why that was worth the extra thousand lines.

**The D3D12 side was verified the way Phases 5 to 7 were: by pushing the
branch.** All 38 land on Windows too — **872/872 on Windows MSVC, Windows Clang,
Windows MSVC ARM64 and Windows Clang ARM64**, against 834 before this phase —
and `MeshRenderTests` is a rendered case, so the depth attachment, the pipeline
state and the base vertex are read back as pixels on that backend rather than
assumed. This entry said "Metal verified" for a day longer than it was true,
which is the same lag the top of this document records for Phases 1 to 4: the
Windows answer arrives about ten minutes after the push, and the only thing
stopping it being written down is remembering to look.

**`main` moved five commits under the branch while it sat there, and this time
it did not collide.** Ranged `Buffer` read and update, three pieces of compute
codegen, and a Windows webview fix — none of them anywhere near the Mesh module,
so the merge was clean and nothing in `Lib/eacp/Mesh` changed to take it. Worth
recording next to Phase 5's collision because it is the same situation with the
opposite outcome: the risk of a long-lived branch is not that `main` moves, it is
that it moves *into the same files*. The one number it changes is `GPUTests`,
which reads **194** on the branch rather than the 191 this phase started from —
`BufferRangeTests` and the codegen cases came from `main`, and none of the three
is this phase's.

`Apps/Model` loads a glTF, draws it with an orbit camera, and reports the node
tree, the materials, the geometry cost and the per-pass GPU timings. It ships a
scene built in memory so launching it is a smoke test of the whole path rather
than a blank window.

Numbers from that sample scene — five nodes, three meshes, 72 vertices:

| | |
| --- | --- |
| `model` pass | 0.067 ms |
| `ui` pass | 0.160 ms |
| Geometry | 2.5 KB, against 3.8 KB unpacked (1.54×) |

The geometry line read 2.2 KB and 1.74× until the UV went back to a `Float2`;
§5.4 is what those four bytes a vertex bought.

The inspector costs more than twice what the model costs, which is the first
thing having both passes labelled is able to say, and exactly the shape Phase 4b
predicted would be worth knowing.

**Three assertions passed for the wrong reason and had to be rewritten.** This
is Phase 5's "a case that passes for one reason is worth as much as one that
fails" arriving again, harder, and it is the single most useful thing this phase
produced:

- **The mirrored-node case passed with the front-face flip deleted.** Culling a
  mirrored cube's near faces simply reveals its *far* faces, which are the same
  colour — so the obvious scene proves nothing. It now parks a green slab inside
  the red cube, between its two faces, so the broken version comes out green.
- **The node-composition case passed with the multiplication reversed**, because
  it composed two *translations* and translations commute. The parent now
  rotates and the child translates.
- **The base-vertex case would have passed without a base vertex at all.** Two
  primitives whose geometry differs only by their node's transform draw
  identically whether or not it is threaded through. The offset had to move into
  the *vertex data* for the draw to be able to get it wrong.

Each was found by breaking the thing deliberately, as in every phase before it.
Ten breaks were run in total — base vertex pinned to zero, the front-face flip
removed, the depth attachment removed, the shading uniform never sent, indices
rebased in the loader, material defaults zero-initialised, the composition
reversed, the normal matrix replaced by the model matrix, the generated normals
wound backwards, TRS reordered — and every one is now caught by precisely the
cases naming it.

**What this section got wrong or did not anticipate:**

- **§5.2 argued for a dependency on its shape and missed what it was.** The
  original paragraph checked that cgltf parses and nothing else, that it needs no
  allocator and drags in no packages — all true, and all beside the point that it
  would have been the first dependency in eacp somebody else wrote. That is a
  change of posture for a framework whose every other package is its author's
  own, and it should have been the first thing the section weighed rather than
  something noticed on review. The loader is now written on `Miro::Json`; §5.2
  carries the argument and the numbers.

- **The estimate for writing it was a third short, and for a reason worth
  remembering.** §5.2 costed the happy path at ~650 lines and it came to 1,038.
  What the estimate left out is that a model file is untrusted input: every read
  bounds-checked, every failure a return value, and `Miro::Json`'s own accessors
  unusable directly because they throw on a type mismatch. Robustness was the
  overrun, not features.

- **Replacing the loader cost no test changes, which is the seam paying out.**
  All 25 existing cases passed against an entirely different parser underneath,
  unmodified — they assert on `MeshData` and author their glTF in-test, exactly as
  §5.7 intended. That is the strongest evidence the `MeshData` seam was worth
  drawing, and it is worth knowing it was tested rather than assumed.

- **But they only proved the old behaviour survived.** Every one of those 25 uses
  float attributes in a base64 `.gltf`, because that is what a hand-written test
  naturally produces — so none of them touched the GLB container, a byte stride, a
  normalized short or a sparse accessor. All four are paths a dependency covered
  before and this tree covers now, and all four needed cases of their own:
  `GltfReaderTests` is those, thirteen of them.

- **A deliberate break found a bug in the tests rather than the code.** Breaking
  the GLB chunk walk made `loadsAGlbContainer` *segfault* instead of fail:
  NanoTest's `check()` records and carries on, `Vector::operator[]` is unchecked
  and `noexcept`, so a case that asserts a load succeeded and then subscripts it
  reads out of bounds when the load fails. In CI that takes the suite's buffered
  output with it and says nothing about which case died. Every case that
  subscripts now guards first. Worth carrying forward well past this phase: an
  assertion framework that continues after a failure makes the *next* line a
  hazard, and the cost is paid at exactly the moment something else has already
  gone wrong.

- **"The indices stay 16-bit" is a check, not a property.** §5.5 stated it
  flatly. A single glTF primitive genuinely can exceed 65536 vertices, so the
  loader keeps indices 32-bit in `MeshData` and `fitsNarrowIndices` decides the
  upload width. The base vertex still buys almost everything the section claimed
  — the question is about the largest *primitive* rather than the model, so a
  200k-vertex model still indexes narrowly — but it is answered rather than
  assumed.
- **The model cannot be drawn underneath the UI, and §5 never noticed.** ImGui's
  pipeline has no depth attachment and a 3D scene needs one; both backends reject
  a draw whose pipeline disagrees with its pass about that. So the scene renders
  into a texture with its own depth buffer and the UI samples it — which needed
  a new hook in this repository's library, `ImGuiView::onBeforePass`. That is the
  second time an app has needed something from `Lib/` that the plan assumed it
  could do from outside, after `DrawRenderer::getPrepareTime()` in Phase 4a, and
  for the same reason: overriding `render()` forks the view's frame logic into
  the app.
- **It made the viewport better rather than worse.** A texture target is a
  resizable panel, so the 3D view is an ImGui window rather than the whole
  surface — which is what an inspector wanted anyway.
- **A glTF's images and its buffers are resolved by different rules**, and the
  spec does not put them side by side. A buffer is a URI or the GLB binary chunk;
  an image is a URI *or a buffer view*, and its URI may be an inline
  `data:image/png;base64,...` — which is the most common single-file `.gltf`
  shape, and yields no textures at all if the loader only resolves buffer URIs.
  Worth knowing because it is the one place the two paths look interchangeable
  and are not: `GltfReader::readUri` serves both, and `loadImages` has to try the
  buffer view *first*.
- **glTF asks for flat normals when a mesh has none**, which needs a vertex per
  face and therefore a different vertex count than the file declares. The loader
  generates area-weighted *smooth* normals instead — same vertex buffer, rounded
  where flat shading would facet. Written down in `GltfLoader.cpp` rather than
  left as a silent deviation.
- **`Float16x4` still has no consumer**, exactly as §5.4 predicted. It is the
  tangent's format and tangents arrive with normal mapping. `Float16x2` had one
  and lost it — see the next bullet — so two of the six packed formats Phase 3
  landed are in use here, not five.

- **The half-float UV was wrong, and every test in the suite agreed with it.**
  This is the finding §5.4 and §5.9 now carry, but the part that belongs in this
  list is *why it survived 38 tests*: every UV assertion anywhere in
  `Tests/Mesh` used a coordinate inside `[0, 1]`, and so did the whole sample
  scene. Not one of them was written to be lenient — `[0, 1]` is simply what a
  hand-authored test writes when it needs a texture coordinate, exactly as a
  base64 `.gltf` of float attributes is what one writes when it needs a file.
  That is the same shape as the finding four bullets up, where 25 tests proved
  only that the old behaviour survived, and it is the more general lesson of this
  phase: **a test authored by hand covers the case a hand reaches for**, and the
  gap it leaves is invisible from inside the suite, because everything in there
  passes. Both times, the fix was to ask what a *file* contains that a *test*
  never happens to.

**And one thing it got right, which is worth as much.** §5.9 asked whether the
corpus contains a mirrored node. It does not have to: the case is authored in
`MeshRenderTests` and in the sample scene the app ships, so the path Phase 5's
`frontFace` field exists for has a consumer and a test rather than a hope.

One open question in §5.9 remains: no model files are vendored, and where a
corpus comes from is unanswered. The tiled-UV question that sat beside it is
closed — see §5.4 — and it closed the way that section predicted, without
needing a corpus to do it.
