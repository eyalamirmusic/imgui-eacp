#pragma once

#include <string>

// A glTF document built in memory, so the app has something to show before
// anyone has pointed it at a file.
//
// It is a real glTF parsed by the real loader rather than a MeshData assembled
// directly — which makes launching the app a smoke test of the whole path, and
// means the thing on screen at startup is evidence rather than decoration.
//
// The scene is chosen to exercise what the inspector has to display: a parent
// with children, so the node tree has depth; three materials, one opaque, one
// translucent and one double-sided, so the renderer builds more than one
// pipeline; vertex colours on one mesh; and a node with a negative scale, which
// is the mirrored case face culling gets wrong without a flipped front face.

namespace ModelApp
{
std::string sampleGltfDocument();
} // namespace ModelApp
