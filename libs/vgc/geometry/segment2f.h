// Copyright 2024 The VGC Developers
// See the COPYRIGHT file at the top-level directory of this distribution
// and at https://github.com/vgc/vgc/blob/master/COPYRIGHT
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This file was automatically generated, please do not edit directly.
// Instead, edit tools/segment2x.h then run tools/generate.py.

// clang-format off

#ifndef VGC_GEOMETRY_SEGMENT2F_H
#define VGC_GEOMETRY_SEGMENT2F_H

#include <vgc/geometry/api.h>
#include <vgc/geometry/vec2f.h>

namespace vgc::geometry {

/// \class vgc::core::Segment2f
/// \brief 2D line segment using single-precision floating points.
///
/// This class represents a 2D line segment using single-precision floating points.
///
/// The segment is internally represented by its start point `a()` and its end
/// point `b()`. This is ideal for storage as it takes a minimal amount of
/// memory.
/// 
/// However, many operations involving segments (such as projections and
/// intersections) require computing the length of the segment and/or the unit
/// vector `(b() - a()).normalized()`. The class `NormalizedSegment2f` also
/// stores this extra information and is ideal for computation tasks.
/// 
/// You can convert a `Segment2f` to a `NormalizedSegment2f` by calling the
/// `normalized()` method.
/// 
// VGC_GEOMETRY_API <- Omitted on purpose.
//                     If needed, manually export individual functions.
class Segment2f {
public:
    using ScalarType = float;
    static constexpr Int dimension = 2;

    VGC_WARNING_PUSH
    VGC_WARNING_MSVC_DISABLE(26495) // member variable uninitialized
    /// Creates an uninitialized `Segment2f`.
    ///
    Segment2f(core::NoInit) noexcept
        : data_{Vec2f(core::noInit), Vec2f(core::noInit)} {
    }
    VGC_WARNING_POP

    /// Creates a zero-initialized `Segment2f`.
    ///
    /// This is equivalent to `Segment2f(0, 0, 0, 0)`.
    ///
    constexpr Segment2f() noexcept
        : data_{} {
    }

    /// Creates a `Segment2f` defined by the two points `a` and `b`.
    ///
    constexpr Segment2f(const Vec2f& a, const Vec2f& b) noexcept
        : data_{a, b} {
    }

    /// Creates a `Segment2f` defined by the two points (`ax`, `ay`) and
    /// (`bx`, `by`).
    ///
    constexpr Segment2f(float ax, float ay, float bx, float by) noexcept
        : data_{Vec2f(ax, ay), Vec2f(bx, by)} {
    }

    /// Accesses the `i`-th point of this `Segment2f`, where `i` must be either
    /// `0` or `1`, corresponsing respectively to `a()` and `b()`.
    ///
    constexpr const Vec2f& operator[](Int i) const {
        return data_[i];
    }

    /// Mutates the `i`-th point of this `Triangle2f`.
    ///
    constexpr Vec2f& operator[](Int i) {
        return data_[i];
    }

    /// Returns the start point of the segment.
    ///
    constexpr const Vec2f& a() const {
        return data_[0];
    }

    /// Returns the end point of the segment.
    ///
    constexpr const Vec2f& b() const {
        return data_[1];
    }

    /// Modifies the start point of the segment.
    /// 
    constexpr void setA(const Vec2f& a) {
        data_[0] = a;
    }

    /// Modifies the end point of the segment.
    /// 
    constexpr void setB(const Vec2f& b) {
        data_[1] = b;
    }

    /// Returns the x-coordinate of the start point of the segment.
    ///
    constexpr float ax() const {
        return data_[0][0];
    }

    /// Returns the y-coordinate of the start point of the segment.
    ///
    constexpr float ay() const {
        return data_[0][1];
    }

    /// Returns the x-coordinate of the end point of the segment.
    ///
    constexpr float bx() const {
        return data_[1][0];
    }

    /// Returns the y-coordinate of the end point of the segment.
    ///
    constexpr float by() const {
        return data_[1][1];
    }

    /// Modifies the x-coordinate of the start point of the segment.
    ///
    constexpr void setAx(float ax) {
        data_[0][0] = ax;
    }

    /// Modifies the y-coordinate of the start point of the segment.
    ///
    constexpr void setAy(float ay) {
        data_[0][1] = ay;
    }

    /// Modifies the x-coordinate of the end point of the segment.
    ///
    constexpr void setBx(float bx) {
        data_[1][0] = bx;
    }

    /// Modifies the y-coordinate of the end point of the segment.
    ///
    constexpr void setBy(float by) {
        data_[1][1] = by;
    }

    /// Returns whether the segment is degenerate, that is, whether it is
    /// reduced to a point.
    ///
    constexpr bool isDegenerate() const {
        return a() == b();
    }

    // TODO
/*
    /// Returns a copy of this rectangle with length and unit tangent/normal precomputed.
    /// 
    constexpr NormalizedSegment2f normalized() {
        return NormalizedSegment2f(*this);
    }
*/

    /// Adds in-place the points of `other` to the points of this `Segment2f`.
    ///
    constexpr Segment2f& operator+=(const Segment2f& other) {
        data_[0] += other[0];
        data_[1] += other[1];
        return *this;
    }

    /// Returns the addition of the two segments `s1` and `s2`.
    ///
    friend constexpr Segment2f operator+(const Segment2f& s1, const Segment2f& s2) {
        return Segment2f(s1) += s2;
    }

    /// Returns a copy of this `Segment2f` (unary plus operator).
    ///
    constexpr Segment2f operator+() const {
        return *this;
    }

    /// Substracts in-place the points of `other` from the points of this `Segment2f`.
    ///
    constexpr Segment2f& operator-=(const Segment2f& other) {
        data_[0] -= other[0];
        data_[1] -= other[1];
        return *this;
    }

    /// Returns the substraction of `s1` and `s2`.
    ///
    friend constexpr Segment2f operator-(const Segment2f& s1, const Segment2f& s2) {
        return Segment2f(s1) -= s2;
    }

    /// Returns the opposite of this `Segment2f` (unary minus operator).
    ///
    constexpr Segment2f operator-() const {
        return Segment2f(-data_[0], -data_[1]);
    }

    /// Multiplies in-place all the points in this `Segment2f` by
    /// the scalar `s`.
    ///
    constexpr Segment2f& operator*=(float s) {
        data_[0] *= s;
        data_[1] *= s;
        return *this;
    }

    /// Returns the multiplication of all the points in this `Segment2f` by
    /// the scalar `s`.
    ///
    constexpr Segment2f operator*(float s) const {
        return Segment2f(*this) *= s;
    }

    /// Returns the multiplication of the scalar `s` with the all the points
    /// in the segment `t`.
    ///
    friend constexpr Segment2f operator*(float s, const Segment2f& segment) {
        return segment * s;
    }

    /// Divides in-place the points of this `Segment2f` by the scalar `s`.
    ///
    constexpr Segment2f& operator/=(float s) {
        data_[0] /= s;
        data_[1] /= s;
        return *this;
    }

    /// Returns the division of the points in this `Segment2f` by the scalar `s`.
    ///
    constexpr Segment2f operator/(float s) const {
        return Segment2f(*this) /= s;
    }

    /// Returns whether `s1` and `s2` are equal.
    ///
    friend constexpr bool operator==(const Segment2f& s1, const Segment2f& s2) {
        return s1.data_[0] == s2.data_[0]
            && s1.data_[1] == s2.data_[1];
    }

    /// Returns whether `s1` and `s2` are different.
    ///
    friend constexpr bool operator!=(const Segment2f& s1, const Segment2f& s2) {
        return s1.data_[0] != s2.data_[0]
            || s1.data_[1] != s2.data_[1];
    }

private:
    Vec2f data_[2];
};

/// Alias for `vgc::core::Array<vgc::geometry::Segment2f>`.
///
using Segment2fArray = core::Array<Segment2f>;

/// Overloads `setZero(T& x)`.
///
/// \sa `vgc::core::zero<T>()`
///
inline void setZero(Segment2f& s) {
    s = Segment2f();
}

/// Writes the given `Segment2f` to the output stream.
///
template<typename OStream>
void write(OStream& out, const Segment2f& s) {
    write(out, '(', s[0], ", ", s[1], ')');
}

/// Reads a `Segment2f` from the input stream, and stores it in the given output
/// parameter `s`. Leading whitespaces are allowed. Raises `ParseError` if the
/// stream does not start with a `Segment2f`. Raises `RangeError` if one of its
/// coordinates is outside the representable range of a float.
///
template<typename IStream>
void readTo(Segment2f& s, IStream& in) {
    skipWhitespacesAndExpectedCharacter(in, '(');
    readTo(s[0], in);
    skipWhitespacesAndExpectedCharacter(in, ',');
    readTo(s[1], in);
    skipWhitespacesAndExpectedCharacter(in, ')');
}

} // namespace vgc::geometry

// see https://fmt.dev/latest/api.html#formatting-user-defined-types
template<>
struct fmt::formatter<vgc::geometry::Segment2f> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }
        return it;
    }
    template<typename FormatContext>
    auto format(const vgc::geometry::Segment2f& s, FormatContext& ctx) {
        return format_to(ctx.out(), "({}, {})", s[0], s[1]);
    }
};

#endif // VGC_GEOMETRY_SEGMENT2F_H