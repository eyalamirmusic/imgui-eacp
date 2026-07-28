#include "FrameStats.h"

#include <algorithm>

namespace Bench
{
void FrameStats::add(float sample)
{
    values[(std::size_t) nextIndex] = sample;

    nextIndex = (nextIndex + 1) % capacity;
    count = std::min(count + 1, capacity);
}

void FrameStats::clear()
{
    nextIndex = 0;
    count = 0;
}

FrameStats::Summary FrameStats::summarize() const
{
    if (count == 0)
        return {};

    // Sorted on a copy, every frame the panel is open. It is a few hundred
    // floats in a UI that is about to build tens of thousands of vertices, and
    // keeping the ring in arrival order is what lets the plot read from it.
    auto sorted = values;
    const auto end = sorted.begin() + count;

    std::sort(sorted.begin(), end);

    const auto at = [&](float fraction)
    {
        const auto index = (int) (fraction * (float) (count - 1) + 0.5f);
        return sorted[(std::size_t) std::clamp(index, 0, count - 1)];
    };

    auto summary = Summary {};

    summary.p50 = at(0.5f);
    summary.p95 = at(0.95f);
    summary.max = sorted[(std::size_t) (count - 1)];
    summary.last = values[(std::size_t) ((nextIndex + capacity - 1) % capacity)];

    return summary;
}
} // namespace Bench
