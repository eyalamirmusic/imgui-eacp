#include "FrameStats.h"
#include "Workload.h"

#include <imgui-eacp/ImGuiEacp.h>

#include <algorithm>
#include <chrono>

// What one frame of this backend costs, against a load that is the same on
// every run.
//
// The tool is drawn by the thing it measures, which is the reason it lives in
// this repo rather than in eacp's: an ImGui app is the natural shape for a
// panel of live numbers, and the ImGui backend is what the numbers are about.
//
// Three groups of them, answering different questions:
//
//   - prepare() and encode(), as percentiles over the last few hundred frames.
//     A mean would hide the thing worth seeing — a buffer growing, an atlas
//     upload — because it lands in one frame out of hundreds.
//   - the geometry the frame moved, which is what the packed vertex and the
//     16-bit index buffer changed.
//   - GPU buffers created, which is what streaming changed. In steady state it
//     reads zero per frame however hard the sliders are pushed; anything else
//     is an allocation in the frame loop.
//
// The numbers are all the previous frame's. ImGui::GetDrawData() is null until
// Render() has run, so a panel drawing itself cannot report its own geometry —
// last frame's is the only answer there is mid-frame, and at 60Hz against a
// fixed load it is the same answer.

using namespace eacp;

namespace
{
float toMicroseconds(std::chrono::nanoseconds time)
{
    return (float) std::chrono::duration<double, std::micro> {time}.count();
}

void drawTiming(const char* label, const Bench::FrameStats& stats)
{
    const auto summary = stats.summarize();

    ImGui::Text("%-8s p50 %7.1f   p95 %7.1f   max %7.1f",
                label,
                summary.p50,
                summary.p95,
                summary.max);

    // Scaled to the window's own worst frame rather than to a fixed ceiling, so
    // the shape of a 3µs encode is as readable as a 300µs one. The plot starts
    // at the oldest sample in the ring, which is where the write cursor sits.
    ImGui::PlotLines(label,
                     stats.samples(),
                     stats.sampleCount(),
                     stats.oldestIndex(),
                     nullptr,
                     0.0f,
                     summary.max * 1.25f,
                     {0.0f, 40.0f});
}

// One frame of GPU time as a strip: each labelled pass a segment as wide as its
// share of the frame, and whatever is left over the part of the frame that was
// not any of them.
//
// A bar rather than a number because the question it answers is a proportion —
// how much of the frame the UI actually is — and because with more than one
// pass a column of milliseconds does not show where the time went.
void drawFlameStrip(const GPU::FrameTimings& timings)
{
    constexpr auto height = 22.0f;

    const auto width = ImGui::GetContentRegionAvail().x;
    const auto origin = ImGui::GetCursorScreenPos();
    const auto bottom = ImVec2 {origin.x + width, origin.y + height};

    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, bottom, IM_COL32(32, 36, 44, 255), 3.0f);

    ImGui::Dummy({width, height});

    if (timings.milliseconds <= 0.0 || width <= 0.0f)
        return;

    auto left = origin.x;

    for (auto index = 0; index < timings.passes.size(); ++index)
    {
        const auto& pass = timings.passes[index];
        const auto share = (float) (pass.milliseconds / timings.milliseconds);
        const auto right = std::min(left + share * width, bottom.x);

        if (right <= left)
            continue;

        const auto color =
            (ImU32) ImColor::HSV(0.55f - (float) index * 0.13f, 0.6f, 0.85f);

        list->AddRectFilled({left, origin.y}, {right, bottom.y}, color, 3.0f);

        // Clipped rather than measured and skipped: a segment too narrow for
        // its name still shows the colour, and the row below names it anyway.
        list->PushClipRect({left, origin.y}, {right, bottom.y}, true);
        list->AddText({left + 4.0f, origin.y + 3.0f},
                      IM_COL32(16, 18, 22, 255),
                      pass.label.c_str());
        list->PopClipRect();

        left = right;
    }
}

struct BenchView final : Gui::ImGuiView
{
    // The pass has to be named for the hardware to time it at all, and this is
    // the app whose whole job is reading those numbers.
    BenchView()
        : Gui::ImGuiView(Gui::ViewOptions {.passLabel = "imgui"})
    {
    }

    void draw() override
    {
        sample();

        const auto bounds = getLocalBounds();
        workload.draw({bounds.w, bounds.h});

        drawReport();
    }

    // Reads what the last frame cost, before this frame's drawing overwrites
    // it. Everything here is a counter the renderer or the device already
    // keeps; the app only puts it in a ring.
    void sample()
    {
        const auto& drawn = getRenderer();

        prepareStats.add(toMicroseconds(drawn.getPrepareTime()));
        encodeStats.add(toMicroseconds(drawn.getEncodeTime()));

        const auto created = GPU::Device::shared().buffersCreated();

        buffersThisFrame = created - lastBufferCount;
        lastBufferCount = created;

        bufferStats.add((float) buffersThisFrame);

        // Only when a new one has arrived. GPU timings come back a few frames
        // late, so the same frame's numbers are still there on the frames in
        // between - adding them again would count one frame several times and
        // quietly weight the percentiles towards whatever the GPU was slowest
        // at.
        const auto& timings = GPU::Device::shared().lastFrameTimings();

        if (timings.frameIndex != lastTimedFrame)
        {
            lastTimedFrame = timings.frameIndex;
            gpuStats.add((float) (timings.milliseconds * 1000.0));
        }
    }

    void drawReport()
    {
        ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({440.0f, 700.0f}, ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("imgui-eacp bench"))
        {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Workload");
        workload.drawControls();

        ImGui::SeparatorText("Frame");

        const auto& io = ImGui::GetIO();
        ImGui::Text("%.1f fps   (%.2f ms)",
                    (double) io.Framerate,
                    1000.0 / (double) io.Framerate);

        ImGui::SeparatorText("Renderer CPU (microseconds)");
        drawTiming("prepare", prepareStats);
        drawTiming("encode", encodeStats);

        drawGpu();
        drawGeometry(io.Framerate);
        drawAllocations();

        if (ImGui::Button("Reset stats"))
        {
            prepareStats.clear();
            encodeStats.clear();
            bufferStats.clear();
            gpuStats.clear();
        }

        ImGui::End();
    }

    // What the hardware spent, as against what the CPU spent above it. The two
    // are not the same question and a frame can be limited by either — which is
    // the whole reason for a section that reads the GPU's own clock rather than
    // inferring anything from the frame rate.
    void drawGpu()
    {
        ImGui::SeparatorText("GPU (microseconds)");

        auto& device = GPU::Device::shared();
        const auto& timings = device.lastFrameTimings();

        if (timings.frameIndex == 0)
        {
            ImGui::TextDisabled("waiting for the first frame to come back");
            return;
        }

        // Timings are for a frame that has already been drawn — a timestamp
        // cannot exist until the GPU has run the work — so the panel says which
        // one rather than implying it is describing the frame on screen.
        ImGui::Text("frame %7.1f      %llu frames back",
                    timings.milliseconds * 1000.0,
                    (unsigned long long) (device.frameIndex() - timings.frameIndex));

        drawTiming("gpu", gpuStats);

        if (!device.supportsPassTimings())
        {
            ImGui::TextDisabled("no per-pass counters on this device");
            return;
        }

        drawFlameStrip(timings);

        for (const auto& pass: timings.passes)
            ImGui::Text("%-8s %7.1f   (%.0f%% of the frame)",
                        pass.label.c_str(),
                        pass.milliseconds * 1000.0,
                        timings.milliseconds > 0.0
                            ? 100.0 * pass.milliseconds / timings.milliseconds
                            : 0.0);
    }

    void drawGeometry(float framerate)
    {
        ImGui::SeparatorText("Geometry");

        const auto& drawn = getRenderer();

        const auto vertexBytes =
            (double) drawn.getVertexCount() * sizeof(Gui::DrawVertex);
        const auto indexBytes = (double) drawn.getIndexCount() * sizeof(ImDrawIdx);
        const auto bytes = vertexBytes + indexBytes;

        ImGui::Text("%d vertices, %d indices, %d draws",
                    drawn.getVertexCount(),
                    drawn.getIndexCount(),
                    drawn.getDrawCount());

        // The width of a vertex and of an index are the two things phases 1 and
        // 3 moved — 32 bytes to 20, and 32-bit indices to ImDrawIdx's own 16 —
        // so the bytes uploaded per frame are where that shows up.
        ImGui::Text(
            "%zu B/vertex, %zu B/index", sizeof(Gui::DrawVertex), sizeof(ImDrawIdx));

        ImGui::Text("%.1f KB/frame   (%.1f MB/s)",
                    bytes / 1024.0,
                    bytes * (double) framerate / (1024.0 * 1024.0));
    }

    void drawAllocations()
    {
        ImGui::SeparatorText("GPU allocations");

        // Geometry goes through GPU::StreamingBuffers, which recycles a buffer
        // once the frame that used it can no longer be on the GPU — so this
        // settles as soon as the pools are warm and stays there however hard
        // the sliders are pushed. Per frame it should read zero; a frame that
        // grew the pools is one that allocated, and a number that never stops
        // climbing is the bug that type exists to prevent.
        ImGui::Text("%d buffers created   (%+d this frame)",
                    lastBufferCount,
                    buffersThisFrame);

        // The worst frame in the window, not just this one. An allocation is a
        // single-frame event, and a live readout of the current frame shows it
        // for one sixtieth of a second and then forgets — which is no use for
        // the one number this app exists to keep honest.
        const auto worst = bufferStats.summarize().max;

        ImGui::Text("worst frame in the last %d: %+d",
                    bufferStats.sampleCount(),
                    (int) worst);

        ImGui::PlotLines("allocations",
                         bufferStats.samples(),
                         bufferStats.sampleCount(),
                         bufferStats.oldestIndex(),
                         nullptr,
                         0.0f,
                         std::max(1.0f, worst),
                         {0.0f, 30.0f});
    }

    Bench::Workload workload;

    Bench::FrameStats prepareStats;
    Bench::FrameStats encodeStats;
    Bench::FrameStats bufferStats;
    Bench::FrameStats gpuStats;

    std::uint64_t lastTimedFrame = 0;

    int lastBufferCount = 0;
    int buffersThisFrame = 0;
};

Graphics::WindowOptions windowOptions()
{
    auto options = Graphics::WindowOptions {};

    options.width = 1280;
    options.height = 800;
    options.minWidth = 720;
    options.minHeight = 480;
    options.title = "imgui-eacp — Bench";
    options.backgroundColor = Graphics::Color {0.09f, 0.10f, 0.13f};

    return options;
}

struct BenchApp
{
    BenchApp() { window.setContentView(view); }

    BenchView view;
    Graphics::Window window {windowOptions()};
};
} // namespace

int main()
{
    return Apps::run<BenchApp>();
}
