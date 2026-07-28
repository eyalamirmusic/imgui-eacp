#pragma once

#include <imgui.h>

namespace Bench
{
// A synthetic UI whose cost is set by sliders instead of by whatever a demo
// window happens to contain. That is the whole point of it: two runs of a
// benchmark have to be measuring the same thing, and the demo window's geometry
// changes with which node of a tree is open.
//
// The three knobs are the three numbers the renderer's per-frame cost is made
// of — how many draw lists it walks, how many draw commands each one holds, and
// how much geometry hangs off each command. A command is a clip rect: ImGui
// starts a new ImDrawCmd whenever the clip rect changes and merges the draws
// that share one, so a band per command is what makes the count exact rather
// than approximate.
//
// Every rect is 4 vertices and 6 indices whether or not it lands on screen, so
// the load is the number the sliders say even when the tiles overflow their
// band. Turned up far enough it crosses 64k vertices in one draw list, which is
// where ImGui splits with a VtxOffset — the case the renderer answers with
// drawIndexed's base vertex, and worth having under measurement.
class Workload
{
public:
    // The sliders, and what they add up to. Drawn inside the report panel.
    void drawControls();

    // The load itself, tiled over a view of this size in logical points.
    void draw(ImVec2 area);

    int getDrawCount() const { return windows * commandsPerWindow; }
    int getVertexCount() const { return getDrawCount() * rectsPerCommand * 4; }
    int getIndexCount() const { return getDrawCount() * rectsPerCommand * 6; }

private:
    void drawWindow(int index, ImVec2 position, ImVec2 size);
    void drawBands(int index);
    void drawTiles(ImDrawList& list, ImVec2 origin, ImVec2 size, int band);

    int windows = 8;
    int commandsPerWindow = 8;
    int rectsPerCommand = 64;

    // Off by default: a still picture is the easier thing to compare two
    // measurements of, and nothing here needs motion to cost what it costs.
    bool animate = false;
};
} // namespace Bench
