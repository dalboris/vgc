// Copyright 2023 The VGC Developers
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

#ifndef VGC_GEOMETRY_CATMULLROM_H
#define VGC_GEOMETRY_CATMULLROM_H

#include <vgc/core/span.h>
#include <vgc/geometry/api.h>
#include <vgc/geometry/bezier.h>
#include <vgc/geometry/interpolatingstroke.h>
#include <vgc/geometry/vec2.h>

namespace vgc::geometry {

namespace detail {

template<typename Point, typename T = scalarType<Point>>
void catmullRomToBezier(
    const Point* points,
    const T* dValues,
    const T* daValues,
    Point* outPoints) {

    T d1 = dValues[0];
    T d2 = dValues[1];
    T d3 = dValues[2];
    T d1a = daValues[0];
    T d2a = daValues[1];
    T d3a = daValues[2];

    Point p1 = points[1];
    if (d1a > 0) {
        T c1 = 2 * d1 + 3 * d1a * d2a + d2;
        T c2 = 3 * d1a * (d1a + d2a);
        p1 = (d1 * points[2] - d2 * points[0] + c1 * points[1]) / c2;
    }

    Point p2 = points[2];
    if (d3a > 0) {
        T c1 = 2 * d3 + 3 * d2a * d3a + d2;
        T c2 = 3 * d3a * (d2a + d3a);
        p2 = (d3 * points[1] - d2 * points[3] + c1 * points[2]) / c2;
    }

    outPoints[0] = points[1];
    outPoints[3] = points[2];
    outPoints[1] = p1;
    outPoints[2] = p2;
}

} // namespace detail

/// Overload of `uniformCatmullRomToBezier` expecting a pointer to a contiguous sequence
/// of 4 control points in input and output.
///
template<typename Point, typename T = scalarType<Point>>
void uniformCatmullRomToBezier(const Point* inFourPoints, Point* outFourPoints) {
    constexpr T k = T(1) / 6; // = 1/6 up to T precision
    Point p1 = inFourPoints[1] + k * (inFourPoints[2] - inFourPoints[0]);
    Point p2 = inFourPoints[2] - k * (inFourPoints[3] - inFourPoints[1]);
    outFourPoints[0] = inFourPoints[1]; // These two lines must be done first in order
    outFourPoints[3] = inFourPoints[2]; // to support inFourPoints == outFourPoints
    outFourPoints[1] = p1;
    outFourPoints[2] = p2;
}

/// Convert four uniform Catmull-Rom control points into the four cubic Bézier
/// control points corresponding to the same cubic curve. The formula is the
/// following:
///
/// \verbatim
/// b0 = c1;
/// b1 = c1 + (c2 - c0) / 6;
/// b2 = c2 - (c3 - c1) / 6;
/// b3 = c2;
/// \endverbatim
///
/// Details:
///
/// The tension parameter k = 1/6 is chosen to ensure that if the
/// Catmull-Rom control points are aligned and uniformly spaced, then
/// the resulting curve is parameterized with constant speed.
///
/// Indeed, a Catmull-Rom curve is generally defined as a sequence of
/// (t[i], c[i]) pairs, and the derivative at p[i] is defined by:
///
/// \verbatim
///            c[i+1] - c[i-1]
///     m[i] = ---------------
///            t[i+1] - t[i-1]
/// \endverbatim
///
/// A *uniform* Catmull-Rom assumes that the "times" or "knot values" t[i] are
/// uniformly spaced, for example: [0, 1, 2, 3, 4, ... ]. The spacing between
/// the t[i]s is chosen to be 1 to match the fact that a Bézier curve is
/// defined for t in [0, 1]; otherwise we'd need an additional factor for the
/// variable subsitution (e.g., if t' = 2*t + 1, then dt' = 2 * dt). With these
/// assumptions, we have t[i+1] - t[i-1] = 2, thus:
///
/// \verbatim
///     m[i] = (c[i+1] - c[i-1]) / 2.
/// \endverbatim
///
/// Now, we recall that for a cubic bezier B(t) defined for t in [0, 1]
/// by the control points b0, b1, b2, b3, we have:
///
/// \verbatim
///     B(t)  = (1-t)^3 b0 + 3(1-t)^2 t b1 + 3(1-t)t^2 b2 + t^3 b3
///     dB/dt = 3(1-t)^2 (b1-b0) + 6(1-t)t(b2-b1) + 3t^2(b3-b2)
/// \endverbatim
///
/// By taking this equation at t = 0, we can deduce that:
///
/// \verbatim
///     b1 = b0 + (1/3) * dB/dt.
/// \endverbatim
///
/// Therefore, if B(t) corresponds to the uniform Catmull-Rom subcurve
/// between c[i] and c[i+1], we have:
///
/// \verbatim
///    b1 = b0 + (1/3) * m[i]
///       = b0 + (1/6) * (c[i+1] - c[i-1])
/// \endverbatim
///
/// Which finishes the explanation why k = 1/6.
///
template<typename Point, typename T = scalarType<Point>>
CubicBezier<Point, T> uniformCatmullRomToBezier(core::ConstSpan<Point, 4> points) {
    std::array<Point, 4> controlPoints;
    uniformCatmullRomToBezier(points.data(), controlPoints.data());
    return CubicBezier<Point, T>(controlPoints);
}

/// Overload of `uniformCatmullRomToBezier` with in/out parameter per point.
///
template<typename Point, typename T = scalarType<Point>>
void uniformCatmullRomToBezier(
    const Point& c0,
    const Point& c1,
    const Point& c2,
    const Point& c3,
    Point& b0,
    Point& b1,
    Point& b2,
    Point& b3) {

    constexpr T k = T(1) / 6; // = 1/6 up to T precision
    b0 = c1;
    b1 = c1 + k * (c2 - c0);
    b2 = c2 - k * (c3 - c1);
    b3 = c2;
}

/// Variant of `uniformCatmullRomToBezier` that caps the tangents
/// to prevent p0p1 and p2p3 from intersecting.
///
template<typename Point, typename T = scalarType<Point>>
void uniformCatmullRomToBezierCapped(const Point* inFourPoints, Point* outFourPoints) {
    constexpr T k = T(1) / 6; // = 1/6 up to T precision
    Point t0 = inFourPoints[2] - inFourPoints[0];
    Point t1 = inFourPoints[3] - inFourPoints[1];
    const T d0 = t0.length();
    const T d1 = t1.length();
    const T maxMagnitude = k * 2 * (inFourPoints[2] - inFourPoints[1]).length();
    Point tangent0 = {};
    Point tangent1 = {};
    if (d0 > 0) {
        tangent0 = (t0 / d0) * (std::min)(maxMagnitude, k * d0);
    }
    if (d1 > 0) {
        tangent1 = (t1 / d1) * (std::min)(maxMagnitude, k * d1);
    }
    Point pt0 = inFourPoints[1] + tangent0;
    Point pt1 = inFourPoints[2] - tangent1;
    outFourPoints[0] = inFourPoints[1]; // These two lines must be done first in order
    outFourPoints[3] = inFourPoints[2]; // to support inFourPoints == outFourPoints
    outFourPoints[1] = pt0;
    outFourPoints[2] = pt1;
}

/// Convert four control points of a Catmull-Rom with centripetal paremetrization
/// into the four cubic Bézier control points corresponding to the segment of the
/// curve interpolating between the second and third points.
///
/// See http://www.cemyuksel.com/research/catmullrom_param/catmullrom.pdf
///
/// Using chordal parameterization prevents cusps and loops.
///
template<typename Point, typename T = scalarType<Point>>
CubicBezier<Point, T> centripetalCatmullRomToBezier(core::ConstSpan<Point, 4> points) {
    const Point* points_ = points.data();
    std::array<T, 3> lengths = {
        (points_[1] - points_[0]).length(),
        (points_[2] - points_[1]).length(),
        (points_[3] - points_[2]).length()};
    return centripetalCatmullRomToBezier<Point, T>(points, lengths);
}

/// Overload of `centripetalCatmullRomToBezier()` that accepts pre-computed lengths.
///
// Note: Without TypeIdentity, the following wouldn't compile:
//
// ```
// std::array<Vec2d, 3> points;
// std::array<double, 3> lengths;
// auto res = centripetalCatmullRomToBezier<Vec2d>();
// ```
//
// Because the compiler would try and fail to deduce `T` by
// matching std::array<double, 3> to core::ConstSpan<T, 3>.
//
// So you would need to explicitly provide both template arguments which isn't
// convenient:
//
// ```
// auto res = centripetalCatmullRomToBezier<Vec2d, double>();
// ```
//
// With TypeIdentity, the compiler doesn't even try to deduce T, and therefore
// use the default value T = scalarType<Vec2d>, and once it knows T = double,
// it can convert the std::array<double, 3> to core::ConstSpan<double, 3>.
//
template<typename Point, typename T = scalarType<Point>>
CubicBezier<Point, T> centripetalCatmullRomToBezier(
    core::ConstSpan<Point, 4> points,
    core::ConstSpan<core::TypeIdentity<T>, 3> lengths) {

    const T* l = lengths.data();
    std::array<T, 3> sqrtLengths = {std::sqrt(l[0]), std::sqrt(l[1]), std::sqrt(l[2])};
    return centripetalCatmullRomToBezier<Point, T>(points, lengths, sqrtLengths);
}

/// Overload of `centripetalCatmullRomToBezier()` that accepts pre-computed lengths
/// and square roots of lengths.
///
template<typename Point, typename T = scalarType<Point>>
CubicBezier<Point, T> centripetalCatmullRomToBezier(
    core::ConstSpan<Point, 4> points,
    core::ConstSpan<core::TypeIdentity<T>, 3> lengths,
    core::ConstSpan<core::TypeIdentity<T>, 3> sqrtLengths) {

    std::array<Point, 4> controlPoints;
    centripetalCatmullRomToBezier<Point, T>(
        points.data(), lengths.data(), sqrtLengths.data(), controlPoints.data());
    return CubicBezier<Point, T>(controlPoints);
}

/// Overload of `centripetalCatmullRomToBezier()` that accepts pre-computed lengths
/// and square roots of lengths, and returns the results via an output parameter.
///
template<typename Point, typename T = scalarType<Point>>
void centripetalCatmullRomToBezier(
    const Point* points,
    const core::TypeIdentity<T>* lengths,
    const core::TypeIdentity<T>* sqrtLengths,
    Point* outPoints) {

    return detail::catmullRomToBezier<Point, T>(points, lengths, sqrtLengths, outPoints);
}

/// Convert four control points of a Catmull-Rom with chordal paremetrization
/// into the four cubic Bézier control points corresponding to the segment of the
/// curve interpolating between the second and third points.
///
/// See http://www.cemyuksel.com/research/catmullrom_param/catmullrom.pdf
///
/// Using chordal parameterization prevents sharp turns when two
/// consecutive points are close together relative to their neighbors.
///
template<typename Point, typename T = scalarType<Point>>
CubicBezier<Point, T> chordalCatmullRomToBezier(core::ConstSpan<Point, 4> points) {
    const Point* points_ = points.data();
    std::array<T, 3> lengths = {
        (points_[1] - points_[0]).length(),
        (points_[2] - points_[1]).length(),
        (points_[3] - points_[2]).length()};
    return chordalCatmullRomToBezier<Point, T>(points, lengths);
}

/// Overload of `chordalCatmullRomToBezier()` that accepts pre-computed lengths.
///
template<typename Point, typename T = scalarType<Point>>
CubicBezier<Point, T> chordalCatmullRomToBezier(
    core::ConstSpan<Point, 4> points,
    core::ConstSpan<core::TypeIdentity<T>, 3> lengths) {

    const T* l = lengths.data();
    std::array<T, 3> sqLengths = {l[0] * l[0], l[1] * l[1], l[2] * l[2]};
    return chordalCatmullRomToBezier<Point, T>(points, lengths, sqLengths);
}

/// Overload of `chordalCatmullRomToBezier()` that accepts pre-computed lengths
/// and squared lengths.
///
template<typename Point, typename T = scalarType<Point>>
CubicBezier<Point, T> chordalCatmullRomToBezier(
    core::ConstSpan<Point, 4> points,
    core::ConstSpan<core::TypeIdentity<T>, 3> lengths,
    core::ConstSpan<core::TypeIdentity<T>, 3> sqLengths) {

    std::array<Point, 4> controlPoints;
    chordalCatmullRomToBezier(
        points.data(), lengths.data(), sqLengths.data(), controlPoints.data());
    return CubicBezier<Point, T>(controlPoints);
}

/// Overload of `chordalCatmullRomToBezier()` that accepts pre-computed lengths
/// and squared lengths, and returns the results via an output parameter.
///
template<typename Point, typename T = scalarType<Point>>
void chordalCatmullRomToBezier(
    const Point* points,
    const core::TypeIdentity<T>* lengths,
    const core::TypeIdentity<T>* sqLengths,
    Point* outPoints) {

    return detail::catmullRomToBezier<Point, T>(points, sqLengths, lengths, outPoints);
}

enum class CatmullRomSplineParameterization : UInt8 {
    Uniform,
    Centripetal,
    Chordal
};

// TODO: immutable version with ConstShared storage
//       & move these classes in a new set of .h/.cpp
class VGC_GEOMETRY_API CatmullRomSplineStroke2d final
    : public AbstractInterpolatingStroke2d {
public:
    CatmullRomSplineStroke2d(
        CatmullRomSplineParameterization parameterization,
        bool isClosed)

        : AbstractInterpolatingStroke2d(isClosed)
        , parameterization_(parameterization) {
    }

    CatmullRomSplineStroke2d(
        CatmullRomSplineParameterization parameterization,
        bool isClosed,
        double constantWidth)

        : AbstractInterpolatingStroke2d(isClosed, constantWidth)
        , parameterization_(parameterization) {
    }

    template<typename TRangePositions, typename TRangeWidths>
    CatmullRomSplineStroke2d(
        CatmullRomSplineParameterization parameterization,
        bool isClosed,
        TRangePositions&& positions,
        TRangeWidths&& widths)

        // clang-format off
        // (disagreement between versions)
        : AbstractInterpolatingStroke2d(
            isClosed,
            std::forward<TRangePositions>(positions),
            std::forward<TRangeWidths>(widths))
        // clang-format on
        , parameterization_(parameterization) {
    }

protected:
    Vec2d evalNonZeroCenterline(Int segmentIndex, double u) const override;

    Vec2d evalNonZeroCenterline(Int segmentIndex, double u, Vec2d& dp) const override;

    StrokeSample2d evalNonZero(Int segmentIndex, double u, double& speed) const override;

    void sampleNonZeroSegment(
        StrokeSample2dArray& out,
        Int segmentIndex,
        const CurveSamplingParameters& params,
        AdaptiveStrokeSampler& sampler) const override;

    StrokeSample2d zeroLengthStrokeSample() const override;

    CubicBezier2d segmentToBezier(Int segmentIndex) const;
    CubicBezier2d segmentToBezier(Int segmentIndex, CubicBezier2d& halfwidths) const;

    CubicBezier1d segmentToNormalReparametrization(Int segmentIndex) const;

protected:
    const StrokeModelInfo& modelInfo_() const override;

    std::unique_ptr<AbstractStroke2d> cloneEmpty_() const override;
    std::unique_ptr<AbstractStroke2d> clone_() const override;
    bool copyAssign_(const AbstractStroke2d* other) override;
    bool moveAssign_(AbstractStroke2d* other) override;

    StrokeBoundaryInfo computeBoundaryInfo_() const override;

protected:
    void updateCache_(
        const core::Array<SegmentComputeData>& baseComputeDataArray) const override;

private:
    // These two cannot be computed separately at the moment.
    mutable Vec2dArray centerlineControlPoints_;
    mutable Vec2dArray halfwidthsControlPoints_;
    mutable Vec2dArray normalReparametrizationControlValues_;

    CatmullRomSplineParameterization parameterization_;
};

} // namespace vgc::geometry

#endif // VGC_GEOMETRY_CATMULLROM_H
