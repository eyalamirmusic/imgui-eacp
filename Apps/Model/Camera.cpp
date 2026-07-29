#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace ModelApp
{
using namespace eacp::Mesh;

namespace
{
constexpr auto pi = 3.14159265358979f;

// Just short of straight up and straight down. At exactly vertical the view
// direction is parallel to the up vector and lookAt's cross product collapses,
// so the picture flips over as it passes through.
constexpr auto pitchLimit = pi * 0.5f - 0.01f;
} // namespace

void OrbitCamera::frame(const MeshData& data)
{
    lookAtPoint = data.boundsCenter();

    // Half the largest edge, floored so a degenerate model - a single flat
    // plane, or one that failed to load - still gets a usable camera rather
    // than a distance of zero.
    radius = std::fmax(data.boundsExtent() * 0.5f, 1.0e-3f);

    // Far enough back that a sphere of that radius fits the vertical field of
    // view, plus a margin so the model is not touching the edges.
    distance = radius / std::tan(fieldOfView * 0.5f) * 1.6f;
}

void OrbitCamera::orbit(float deltaYaw, float deltaPitch)
{
    yaw += deltaYaw;
    pitch = std::clamp(pitch + deltaPitch, -pitchLimit, pitchLimit);
}

void OrbitCamera::zoom(float factor)
{
    // Multiplicative rather than additive, so a scroll click moves the same
    // proportion of the way in whether the camera is close or far. Additive
    // zoom crawls when far away and shoots through the model when near.
    distance = std::clamp(distance * factor, radius * 0.05f, radius * 200.0f);
}

void OrbitCamera::pan(float deltaX, float deltaY)
{
    // Along the camera's own axes, and scaled by distance so a drag moves the
    // model the same amount on screen at any zoom.
    auto forward = normalize(lookAtPoint - position());
    auto right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    auto up = cross(right, forward);

    auto scale = distance * 0.001f;
    lookAtPoint = lookAtPoint + right * (-deltaX * scale) + up * (deltaY * scale);
}

Vec3 OrbitCamera::position() const
{
    auto cosPitch = std::cos(pitch);

    return {lookAtPoint.x + distance * cosPitch * std::sin(yaw),
            lookAtPoint.y + distance * std::sin(pitch),
            lookAtPoint.z + distance * cosPitch * std::cos(yaw)};
}

Mat4 OrbitCamera::view() const
{
    return Mat4::lookAt(position(), lookAtPoint, {0.0f, 1.0f, 0.0f});
}

float OrbitCamera::nearPlane() const
{
    return std::fmax(distance * 0.01f, radius * 1.0e-3f);
}

float OrbitCamera::farPlane() const
{
    return distance + radius * 4.0f;
}

Mat4 OrbitCamera::projection(float aspect) const
{
    return Mat4::perspective(aspect, fieldOfView, nearPlane(), farPlane());
}

Vec3 OrbitCamera::lightDirection() const
{
    // Off the camera's own axis rather than straight down it: a light exactly at
    // the eye flattens everything, because every surface facing the camera is
    // then lit identically and the form disappears.
    auto toCamera = normalize(position() - lookAtPoint);
    auto right = normalize(cross({0.0f, 1.0f, 0.0f}, toCamera));

    return normalize(toCamera + right * 0.4f + Vec3 {0.0f, 0.45f, 0.0f});
}
} // namespace ModelApp
