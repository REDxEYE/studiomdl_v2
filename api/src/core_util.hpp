// Call-time helpers of the Core part, also usable by the other parts: rotations and name checks.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

#include "api_util.hpp"

namespace smdl_api {

// Case-insensitive ASCII equality, the comparison studiomdl uses for bone, material and sequence names
inline bool IEquals(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

// x, y, z, w quaternion + position -> the RadianEuler the SMD loaders store (same math as the DMX loader: quaternion to
// matrix, matrix to euler). The quaternion is normalized only when it is visibly not a unit quaternion, so a unit
// quaternion goes through unchanged. Returns false for a zero-length quaternion.
inline bool QuaternionToRadianEuler(const float quaternion[4], const Vector &position, RadianEuler &out) {
    Quaternion q(quaternion[0], quaternion[1], quaternion[2], quaternion[3]);
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!(lengthSquared > 1e-12f))
        return false;
    if (std::fabs(lengthSquared - 1.0f) > 1e-6f)
        QuaternionNormalize(q);
    matrix3x4_t matrix;
    QuaternionMatrix(q, position, matrix);
    Vector unusedPosition;
    MatrixAngles(matrix, out, unusedPosition);
    // the same rotation must give the same bytes: no -0 from the conversion
    for (float *angle: {&out.x, &out.y, &out.z}) {
        if (*angle == 0.0f)
            *angle = 0.0f;
    }
    return true;
}

inline Vector ToVector(const float v[3]) { return Vector(v[0], v[1], v[2]); }

inline bool AllFinite(const float *values, size_t count) {
    return std::all_of(values, values + count, [](float f) { return std::isfinite(f); });
}

} // namespace smdl_api
