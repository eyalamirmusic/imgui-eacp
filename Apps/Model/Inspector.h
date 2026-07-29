#pragma once

#include <eacp/Mesh/Mesh.h>

#include <string>

// The panels that report what was loaded and what it costs to draw.
//
// This is what the whole phase's split falls out of: the loader and the renderer
// are in eacp, where anything can use them, and the part that wants an
// immediate-mode UI is here. None of these panels reaches into a GPU type — they
// read MeshData and RenderStats, which is the same surface a test reads.

namespace ModelApp
{
using namespace eacp;

struct InspectorState
{
    // Which node the tree has selected, or -1. The viewport draws its bounds so
    // that selecting a name in the tree says which part of the model it is.
    int selectedNode = -1;

    bool showNodes = true;
    bool showMaterials = true;
    bool showTiming = true;
};

// The node hierarchy, as a tree. Nodes with no mesh are still shown — a scene
// with an empty root that positions everything under it is the common shape, and
// hiding those makes the tree lie about its own depth.
void drawNodeTree(const Mesh::MeshData& data, InspectorState& state);

// Per-model totals plus the material list, with what each material resolved to.
void drawModelSummary(const Mesh::MeshData& data);
void drawMaterials(const Mesh::MeshData& data);

// What the last frame cost: the draws and triangles the renderer issued, and
// what the GPU reported for the passes it was asked to time.
void drawFrameCost(const Mesh::RenderStats& stats,
                   const std::string& loadError,
                   int textureCount);
} // namespace ModelApp
