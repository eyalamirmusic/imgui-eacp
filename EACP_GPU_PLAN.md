# eacp GPU module: the gaps this backend exposes, and the plan to close them

Building this backend against eacp's GPU module surfaced three things the module
does not offer, each of which `DrawRenderer` works around in a different way.
None of the workarounds is wrong; all three belong in `eacp-gpu` instead, where
`Sprites`, `Text` and any future mesh renderer get them too.

This document is the plan to move them. It starts with the working setup,
because the whole plan involves changing two repositories at once and the build
wiring for that is the first thing to get right.

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
`D3D12_QUERY_TYPE_TIMESTAMP` plus `GetTimestampFrequency` on D3D12. A minimal
API that fits the existing shapes:

```cpp
// On Frame: records a timestamp at this point in the command buffer. Results
// resolve after the GPU has finished the frame, so they arrive framesInFlight
// frames late — a profiler reads the recent past, not the current frame.
void mark(std::string_view label);
```

with `Device::lastFrameTimings()` handing back label → microseconds, and the
`Bench` app rendering it as a flame strip.

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

**Phase 4 — the `Bench` app and timestamp queries (§2.2).** Fold Layer 1 in
alongside Phase 2, since it is what shows Phase 2 worked. Layer 2 after Phase 3.

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
