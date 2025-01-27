#ifndef CSRT__TENSOR__VEC3_HPP
#define CSRT__TENSOR__VEC3_HPP

#include "../defs.hpp"
#include "vec2.hpp"

namespace csrt
{

struct Uvec3
{
    uint32_t x, y, z;

    QUALIFIER_D_H Uvec3() : x(0), y(0), z(0) {}
    QUALIFIER_D_H Uvec3(const uint32_t x) : x(x), y(x), z(x) {}
    QUALIFIER_D_H Uvec3(const uint32_t x, const uint32_t y, const uint32_t z);

    QUALIFIER_D_H uint32_t &operator[](const int i);
    QUALIFIER_D_H uint32_t operator[](const int i) const;
};

struct Vec3
{
    float x;
    float y;
    float z;

    QUALIFIER_D_H Vec3() : x(0), y(0), z(0) {}
    QUALIFIER_D_H Vec3(const float x) : x(x), y(x), z(x) {}
    QUALIFIER_D_H Vec3(const Vec2 &vec, float z) : x(vec.x), y(vec.y), z(z) {}
    QUALIFIER_D_H Vec3(const float x, const float y, const float z);

    QUALIFIER_D_H float &operator[](const int i);
    QUALIFIER_D_H float operator[](const int i) const;

    QUALIFIER_D_H void operator=(const Vec3 &vec);

    QUALIFIER_D_H const Vec3 &operator+() const { return *this; }
    QUALIFIER_D_H Vec3 operator-() const { return {-x, -y, -z}; }

    QUALIFIER_D_H Vec3 &operator+=(const Vec3 &vec);
    QUALIFIER_D_H Vec3 &operator-=(const Vec3 &vec);
    QUALIFIER_D_H Vec3 &operator*=(const Vec3 &vec);
    QUALIFIER_D_H Vec3 &operator/=(const Vec3 &vec);

    QUALIFIER_D_H Vec3 &operator*=(const float t);
    QUALIFIER_D_H Vec3 &operator/=(const float t);
};

QUALIFIER_D_H Vec3 operator+(const Vec3 &v1, const Vec3 &v2);
QUALIFIER_D_H Vec3 operator-(const Vec3 &v1, const Vec3 &v2);
QUALIFIER_D_H Vec3 operator*(const Vec3 &v1, const Vec3 &v2);
QUALIFIER_D_H Vec3 operator/(const Vec3 &v1, const Vec3 &v2);

QUALIFIER_D_H Vec3 operator+(const Vec3 &vec, const float t);
QUALIFIER_D_H Vec3 operator-(const Vec3 &vec, const float t);
QUALIFIER_D_H Vec3 operator*(const Vec3 &vec, const float t);
QUALIFIER_D_H Vec3 operator/(const Vec3 &vec, const float t);

QUALIFIER_D_H Vec3 operator+(const float t, const Vec3 &vec);
QUALIFIER_D_H Vec3 operator-(const float t, const Vec3 &vec);
QUALIFIER_D_H Vec3 operator*(const float t, const Vec3 &vec);

QUALIFIER_D_H float Length(const Vec3 &vec);
QUALIFIER_D_H Vec3 Normalize(const Vec3 &vec);

QUALIFIER_D_H float Dot(const Vec3 &v1, const Vec3 &v2);
QUALIFIER_D_H Vec3 Cross(const Vec3 &v1, const Vec3 &v2);
QUALIFIER_D_H Vec3 Min(const Vec3 &v1, const Vec3 &v2);
QUALIFIER_D_H Vec3 Max(const Vec3 &v1, const Vec3 &v2);
QUALIFIER_D_H Vec3 Sqrt(const Vec3 &v);

} // namespace csrt

#endif