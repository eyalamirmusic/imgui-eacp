#pragma once

#include <array>

namespace Bench
{
// A rolling window of one per-frame measurement, reported as percentiles.
//
// Percentiles rather than a mean, because of what this app is watching for. A
// buffer growing, a GPU allocation, an atlas upload — each lands in a handful
// of frames out of hundreds, so a mean divides it away to nothing while a p95
// and a max show it as the spike it is.
class FrameStats
{
public:
    struct Summary
    {
        float p50 = 0.0f;
        float p95 = 0.0f;
        float max = 0.0f;
        float last = 0.0f;
    };

    void add(float sample);
    void clear();

    Summary summarize() const;

    // The window as ImGui::PlotLines wants it: the raw ring, plus the index of
    // the oldest sample for it to start reading from, which is the write cursor
    // once the ring has wrapped.
    const float* samples() const { return values.data(); }
    int sampleCount() const { return count; }
    int oldestIndex() const { return count < capacity ? 0 : nextIndex; }

    // Around eight seconds at a 60Hz refresh: long enough that a once-a-second
    // spike is in the window, short enough that changing a slider shows up
    // rather than being averaged against the load before it.
    static constexpr int capacity = 512;

private:
    std::array<float, capacity> values {};

    int nextIndex = 0;
    int count = 0;
};
} // namespace Bench
