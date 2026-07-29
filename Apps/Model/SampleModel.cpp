#include "SampleModel.h"

#include <cstdint>
#include <vector>

namespace ModelApp
{
namespace
{
const char* base64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(const std::vector<std::uint8_t>& bytes)
{
    auto result = std::string {};
    result.reserve((bytes.size() + 2) / 3 * 4);

    for (auto i = std::size_t {0}; i < bytes.size(); i += 3)
    {
        auto remaining = bytes.size() - i;

        auto word = (std::uint32_t) bytes[i] << 16;
        word |= (std::uint32_t) (remaining > 1 ? bytes[i + 1] : 0) << 8;
        word |= (std::uint32_t) (remaining > 2 ? bytes[i + 2] : 0);

        result += base64Alphabet[(word >> 18) & 0x3f];
        result += base64Alphabet[(word >> 12) & 0x3f];
        result += remaining > 1 ? base64Alphabet[(word >> 6) & 0x3f] : '=';
        result += remaining > 2 ? base64Alphabet[word & 0x3f] : '=';
    }

    return result;
}

class Payload
{
public:
    int appendFloats(const std::vector<float>& values)
    {
        auto offset = (int) bytes.size();

        for (auto value: values)
        {
            const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
            bytes.insert(bytes.end(), raw, raw + sizeof(float));
        }

        return offset;
    }

    int appendIndices(const std::vector<std::uint16_t>& values)
    {
        auto offset = (int) bytes.size();

        for (auto value: values)
        {
            const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
            bytes.insert(bytes.end(), raw, raw + sizeof(std::uint16_t));
        }

        return offset;
    }

    int size() const { return (int) bytes.size(); }
    std::string uri() const
    {
        return "data:application/octet-stream;base64," + encodeBase64(bytes);
    }

    std::vector<std::uint8_t> bytes;
};

// Four corners per face rather than eight shared ones, so each face gets its own
// outward normal from the loader's generator - a cube sharing corners averages
// three faces at each and comes out looking inflated.
std::vector<float> cubePositions()
{
    return {-1, -1, 1,  1,  -1, 1,  1,  1,  1,  -1, 1,  1, // +z
            1,  -1, -1, -1, -1, -1, -1, 1,  -1, 1,  1,  -1, // -z
            1,  -1, 1,  1,  -1, -1, 1,  1,  -1, 1,  1,  1, // +x
            -1, -1, -1, -1, -1, 1,  -1, 1,  1,  -1, 1,  -1, // -x
            -1, 1,  1,  1,  1,  1,  1,  1,  -1, -1, 1,  -1, // +y
            -1, -1, -1, 1,  -1, -1, 1,  -1, 1,  -1, -1, 1}; // -y
}

// One colour per face, so the vertex-colour path is visible rather than a guess.
std::vector<float> cubeColors()
{
    constexpr float faceColors[6][3] = {{0.95f, 0.35f, 0.30f},
                                        {0.30f, 0.65f, 0.95f},
                                        {0.95f, 0.75f, 0.30f},
                                        {0.45f, 0.85f, 0.50f},
                                        {0.80f, 0.55f, 0.95f},
                                        {0.55f, 0.60f, 0.70f}};

    auto colors = std::vector<float> {};

    for (const auto& face: faceColors)
        for (auto corner = 0; corner < 4; ++corner)
            colors.insert(colors.end(), {face[0], face[1], face[2], 1.0f});

    return colors;
}

std::vector<std::uint16_t> cubeIndices()
{
    auto indices = std::vector<std::uint16_t> {};

    for (auto face = 0; face < 6; ++face)
    {
        auto base = (std::uint16_t) (face * 4);

        indices.insert(indices.end(),
                       {base,
                        (std::uint16_t) (base + 1),
                        (std::uint16_t) (base + 2),
                        base,
                        (std::uint16_t) (base + 2),
                        (std::uint16_t) (base + 3)});
    }

    return indices;
}
} // namespace

std::string sampleGltfDocument()
{
    auto positions = cubePositions();
    auto colors = cubeColors();
    auto indices = cubeIndices();

    auto payload = Payload {};
    auto positionAt = payload.appendFloats(positions);
    auto colorAt = payload.appendFloats(colors);
    auto indexAt = payload.appendIndices(indices);

    auto number = [](auto value) { return std::to_string(value); };

    return std::string {R"({
  "asset": {"version": "2.0", "generator": "imgui-eacp Model app"},
  "scene": 0,
  "scenes": [{"name": "sample", "nodes": [0, 3, 4]}],
  "nodes": [
    {"name": "turntable", "rotation": [0, 0.2588, 0, 0.9659], "children": [1, 2]},
    {"name": "painted cube", "translation": [0, 1.2, 0],
     "scale": [0.8, 0.8, 0.8], "mesh": 0},
    {"name": "glass cube", "translation": [1.9, 1.2, 0],
     "scale": [0.8, 0.8, 0.8], "mesh": 1},
    {"name": "plinth", "translation": [0, -0.25, 0],
     "scale": [3.2, 0.25, 3.2], "mesh": 2},
    {"name": "mirrored cube", "translation": [-1.9, 1.2, 0],
     "scale": [-0.8, 0.8, 0.8], "mesh": 0}
  ],
  "materials": [
    {"name": "painted",
     "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}},
    {"name": "glass", "alphaMode": "BLEND",
     "pbrMetallicRoughness": {"baseColorFactor": [0.55, 0.8, 0.95, 0.35]}},
    {"name": "plinth", "doubleSided": true,
     "pbrMetallicRoughness": {"baseColorFactor": [0.35, 0.36, 0.4, 1]}}
  ],
  "meshes": [
    {"name": "painted cube", "primitives": [
      {"attributes": {"POSITION": 0, "COLOR_0": 1}, "indices": 2, "material": 0}]},
    {"name": "glass cube", "primitives": [
      {"attributes": {"POSITION": 0}, "indices": 2, "material": 1}]},
    {"name": "plinth", "primitives": [
      {"attributes": {"POSITION": 0}, "indices": 2, "material": 2}]}
  ],
  "buffers": [{"uri": ")"}
           + payload.uri() + R"(", "byteLength": )" + number(payload.size()) + R"(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )"
           + number(positionAt) + R"(, "byteLength": )"
           + number((int) positions.size() * 4) + R"(},
    {"buffer": 0, "byteOffset": )"
           + number(colorAt) + R"(, "byteLength": )"
           + number((int) colors.size() * 4) + R"(},
    {"buffer": 0, "byteOffset": )"
           + number(indexAt) + R"(, "byteLength": )"
           + number((int) indices.size() * 2) + R"(}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": )"
           + number((int) positions.size() / 3) + R"(, "type": "VEC3",
     "min": [-1, -1, -1], "max": [1, 1, 1]},
    {"bufferView": 1, "componentType": 5126, "count": )"
           + number((int) colors.size() / 4) + R"(, "type": "VEC4"},
    {"bufferView": 2, "componentType": 5123, "count": )"
           + number((int) indices.size()) + R"(, "type": "SCALAR"}
  ]
})";
}
} // namespace ModelApp
