#include "Workload.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Bench
{
namespace
{
// Fixed, unfocusable and unmovable. A window the mouse can pick up is a window
// whose position is part of the measurement, and the load is meant to be the
// same picture every run.
constexpr auto loadWindowFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
    | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
    | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNavFocus
    | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

constexpr auto tileStep = 8.0f;
constexpr auto tileSize = 6.0f;
constexpr auto margin = 8.0f;
} // namespace

void Workload::drawControls()
{
    ImGui::SliderInt("draw lists", &windows, 1, 32);
    ImGui::SliderInt("commands / list", &commandsPerWindow, 1, 32);
    ImGui::SliderInt("rects / command", &rectsPerCommand, 1, 512);
    ImGui::Checkbox("animate", &animate);

    // What the sliders ask for, which the panel's geometry line then reports
    // the renderer's own count against: the difference is the window chrome and
    // the panel itself, and it should stay small and constant.
    ImGui::TextDisabled("asking for %d draws, %d vertices, %d indices",
                        getDrawCount(),
                        getVertexCount(),
                        getIndexCount());
}

void Workload::drawTiles(ImDrawList& list, ImVec2 origin, ImVec2 size, int band)
{
    const auto perRow = std::max(1, (int) (size.x / tileStep));
    const auto height = std::max(size.y, tileStep);

    const auto time = (float) ImGui::GetTime();
    const auto shift = animate ? std::fmod(time * 20.0f, tileStep) : 0.0f;

    const auto color = (ImU32) ImColor::HSV((float) band * 0.11f, 0.55f, 0.85f);

    for (auto index = 0; index < rectsPerCommand; ++index)
    {
        const auto column = (float) (index % perRow);
        const auto row = (float) (index / perRow);

        const auto x = origin.x + column * tileStep + shift;
        const auto y = origin.y + std::fmod(row * tileStep, height);

        list.AddRectFilled({x, y}, {x + tileSize, y + tileSize}, color);
    }
}

void Workload::drawBands(int index)
{
    auto* list = ImGui::GetWindowDrawList();

    const auto origin = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    const auto height = available.y / (float) commandsPerWindow;

    for (auto band = 0; band < commandsPerWindow; ++band)
    {
        const auto top = origin.y + height * (float) band;

        // The clip rect is what splits the geometry into separate ImDrawCmds,
        // so there is one per band by construction rather than by luck.
        list->PushClipRect(
            {origin.x, top}, {origin.x + available.x, top + height}, true);

        drawTiles(*list, {origin.x, top}, {available.x, height}, band + index);

        list->PopClipRect();
    }
}

void Workload::drawWindow(int index, ImVec2 position, ImVec2 size)
{
    char name[32];
    std::snprintf(name, sizeof(name), "##load%d", index);

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    if (ImGui::Begin(name, nullptr, loadWindowFlags))
        drawBands(index);

    ImGui::End();
}

void Workload::draw(ImVec2 area)
{
    const auto columns = (int) std::ceil(std::sqrt((float) windows));
    const auto rows = (windows + columns - 1) / columns;

    const auto cell = ImVec2 {(area.x - margin) / (float) columns - margin,
                              (area.y - margin) / (float) rows - margin};

    if (cell.x < tileStep || cell.y < tileStep)
        return;

    for (auto index = 0; index < windows; ++index)
    {
        const auto column = (float) (index % columns);
        const auto row = (float) (index / columns);

        drawWindow(
            index,
            {margin + column * (cell.x + margin), margin + row * (cell.y + margin)},
            cell);
    }
}
} // namespace Bench
