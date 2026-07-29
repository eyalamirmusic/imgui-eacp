#pragma once

#include <eacp/Mesh/Mesh.h>

// An orbit camera: a point it looks at, a distance from it, and two angles.
//
// Deliberately not a free-flying camera. An inspector is always looking *at*
// something, and the one operation it needs is "show me the other side of this",
// which is one drag rather than a fly-around.

namespace ModelApp
{
using namespace eacp;

class OrbitCamera
{
public:
    // Puts the whole model on screen: the target is its centre and the distance
    // is worked out from its size and the field of view, so a model authored in
    // metres and one authored in centimetres both arrive framed.
    void frame(const Mesh::MeshData& data);

    void orbit(float deltaYaw, float deltaPitch);
    void zoom(float factor);
    void pan(float deltaX, float deltaY);

    Mesh::Mat4 view() const;
    Mesh::Mat4 projection(float aspect) const;

    Mesh::Vec3 position() const;

    // Where the light is put, so the lit side is the side being looked at. A
    // fixed world-space light leaves the model unlit exactly when the camera
    // moves round to the dark side, which in an inspector is the moment you
    // most want to see it.
    Mesh::Vec3 lightDirection() const;

    float distanceToTarget() const { return distance; }
    Mesh::Vec3 target() const { return lookAtPoint; }

    float fieldOfView = 0.8f;

private:
    Mesh::Vec3 lookAtPoint;

    float distance = 5.0f;
    float yaw = 0.7f;
    float pitch = 0.5f;

    // The near plane is derived from the distance rather than fixed. A fixed
    // 0.1 on a model a thousand units across spends almost all of the depth
    // buffer's precision in the first fraction of the view, and the model
    // z-fights with itself at the far end.
    float nearPlane() const;
    float farPlane() const;

    // How far the model reaches from its centre, which is what both planes and
    // the framing distance are scaled from.
    float radius = 1.0f;
};
} // namespace ModelApp
