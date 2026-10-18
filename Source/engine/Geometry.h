#pragma once

#include <array>
#include <cmath>

namespace vae
{
    struct Vec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;

        Vec3 operator+ (Vec3 o) const noexcept { return { x + o.x, y + o.y, z + o.z }; }
        Vec3 operator- (Vec3 o) const noexcept { return { x - o.x, y - o.y, z - o.z }; }
        Vec3 operator* (float s) const noexcept { return { x * s, y * s, z * s }; }

        float lengthSquared() const noexcept { return x * x + y * y + z * z; }
        float length() const noexcept        { return std::sqrt (lengthSquared()); }

        Vec3 normalised() const noexcept
        {
            const auto len = length();
            return len > 1.0e-6f ? *this * (1.0f / len) : Vec3 { 0.0f, 0.0f, 1.0f };
        }
    };

    // Unit vector for the sphere's spherical coordinates: azimuth turns around
    // the vertical axis (0 deg faces the viewer), elevation tilts towards the poles.
    inline Vec3 directionFromAngles (float azimuthDegrees, float elevationDegrees) noexcept
    {
        constexpr float toRadians = 3.14159265358979f / 180.0f;
        const auto az = azimuthDegrees * toRadians;
        const auto el = elevationDegrees * toRadians;
        return { std::cos (el) * std::sin (az), std::sin (el), std::cos (el) * std::cos (az) };
    }

    // Row-major 3x3 rotation matrix, used by the sphere view's camera.
    struct Mat3
    {
        std::array<float, 9> m { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

        static Mat3 rotationX (float a) noexcept
        {
            const auto c = std::cos (a), s = std::sin (a);
            return { { 1, 0, 0, 0, c, -s, 0, s, c } };
        }

        static Mat3 rotationY (float a) noexcept
        {
            const auto c = std::cos (a), s = std::sin (a);
            return { { c, 0, s, 0, 1, 0, -s, 0, c } };
        }

        Mat3 operator* (const Mat3& o) const noexcept
        {
            Mat3 r;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    r.m[(size_t) (i * 3 + j)] = m[(size_t) (i * 3)] * o.m[(size_t) j]
                                              + m[(size_t) (i * 3 + 1)] * o.m[(size_t) (3 + j)]
                                              + m[(size_t) (i * 3 + 2)] * o.m[(size_t) (6 + j)];
            return r;
        }

        Vec3 apply (Vec3 p) const noexcept
        {
            return { m[0] * p.x + m[1] * p.y + m[2] * p.z,
                     m[3] * p.x + m[4] * p.y + m[5] * p.z,
                     m[6] * p.x + m[7] * p.y + m[8] * p.z };
        }

        // Re-orthonormalises the rows so repeated small rotations don't drift.
        Mat3 orthonormalised() const noexcept
        {
            const auto a = Vec3 { m[0], m[1], m[2] }.normalised();
            auto b = Vec3 { m[3], m[4], m[5] };
            const auto d = a.x * b.x + a.y * b.y + a.z * b.z;
            b = (b - a * d).normalised();
            const Vec3 c { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
            return { { a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z } };
        }
    };
}
