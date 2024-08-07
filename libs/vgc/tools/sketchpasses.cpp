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

#include <vgc/tools/sketchpasses.h>

#include <array>
#include <optional>

#include <vgc/canvas/debugdraw.h>
#include <vgc/core/assert.h>
#include <vgc/core/colors.h>
#include <vgc/geometry/bezier.h>
#include <vgc/geometry/catmullrom.h>
#include <vgc/tools/logcategories.h>

namespace vgc::tools {

namespace {

constexpr bool enableDebugFits = false;

}

void EmptyPass::doUpdateFrom(const SketchPointBuffer& input, SketchPointBuffer& output) {

    // Remove all previously unstable points.
    //
    Int oldNumStablePoints = output.numStablePoints();
    output.resize(oldNumStablePoints);

    // Add all other points (some of which are now stable, some of which are
    // still unstable).
    //
    output.extend(input.begin() + oldNumStablePoints, input.end());

    // Set the new number of stable points as being the same as the input.
    //
    output.setNumStablePoints(input.numStablePoints());

    // Note: there is no need to compute the output chord lengths in the
    // EmptyPass since they are the same as the input chord length.
}

void TransformPass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    // Remove all previously unstable points.
    //
    Int oldNumStablePoints = output.numStablePoints();
    output.resize(oldNumStablePoints);

    // Add all other points (some of which are now stable, some of which are
    // still unstable).
    //
    for (auto it = input.begin() + oldNumStablePoints; it != input.end(); ++it) {
        SketchPoint p = *it;
        p.setPosition(transformAffine(p.position()));
        output.append(p);
    }

    // Updates chord lengths.
    //
    output.updateChordLengths();

    // Set the new number of stable points as being the same as the input.
    //
    output.setNumStablePoints(input.numStablePoints());
}

RemoveDuplicatesPass::RemoveDuplicatesPass()
    : SketchPass()
    , settings_() {
}

RemoveDuplicatesPass::RemoveDuplicatesPass(const RemoveDuplicatesSettings& settings)
    : SketchPass()
    , settings_(settings) {
}

void RemoveDuplicatesPass::doReset() {
    startInputIndex_ = 0;
}

namespace {

void checkCanChangeSettings(const SketchPass& pass) {
    if (pass.output().numStablePoints() > 0) {
        throw core::LogicError( //
            "Cannot change settings of a sketch pass "
            "while points are still being processed.");
    }
}

} // namespace

void RemoveDuplicatesPass::setSettings(const RemoveDuplicatesSettings& settings) {
    checkCanChangeSettings(*this);
    settings_ = settings;
}

namespace {

double squaredDistance(const SketchPoint& p, const SketchPoint& q) {
    return (q.position() - p.position()).squaredLength();
}

} // namespace

// Each output point is the result of merging several input points:
//
// An output point j1 is stable if and only if:
// - It has a next output point j2 = j1 + 1
// - The first input point correponding to j2, which we call i2,
//   is stable.
//
//                           i2
//                stable     v
//          _________________
//         ╱                 ╲
// input:  .     .     ..    ...    .   ..    .
// output: .     .     .     .      .   .     .
//         ╲___________╱
//             stable  ^     ^
//                     j1    j2
//
//
//                           i2
//                stable     v
//          __________________
//         ╱                  ╲
// input:  .     .     ..    ...    .   ..    .
// output: .     .     .     .      .   .     .
//         ╲___________╱
//             stable  ^     ^
//                     j1    j2
//
//
//                           i2
//                stable     v
//          ___________________
//         ╱                   ╲
// input:  .     .     ..    ...    .   ..    .
// output: .     .     .     .      .   .     .
//         ╲___________╱
//             stable  ^     ^
//                     j1    j2
//
//
//                                  i2
//                stable            v
//          ________________________
//         ╱                        ╲
// input:  .     .     ..    ...    .   ..    .
// output: .     .     .     .      .   .     .
//         ╲_________________╱
//             stable        ^      ^
//                           j1     j2
//
//
// We store i2 as startInputIndex_: this is the input index from which we
// should start in the next update.
//
void RemoveDuplicatesPass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    // Remove all previously unstable points.
    //
    Int oldNumStablePoints = output.numStablePoints();
    output.resize(oldNumStablePoints);

    // Add the first input point (if any) if not already in the output.
    //
    Int numInputPoints = input.length();
    if (numInputPoints == 0) {
        VGC_ASSERT(oldNumStablePoints == 0);
        return;
    }
    Int i = startInputIndex_;
    if (output.length() == 0) {
        const SketchPoint& firstPoint = input.first();
        output.append(firstPoint);
        ++i;
    }

    // Add all other points.
    //
    double ds = settings_.distanceThreshold();
    double ds2 = ds * ds;
    if (ds < 0) {
        // If ds is a negative value, we keep all input points.
        // If ds == 0, we only remove points with exactly the same position
        ds2 = -1;
    }
    Int newNumStablePoints = oldNumStablePoints;
    Int lastNonDuplicateInputIndex = startInputIndex_;
    const SketchPointArray& inputData = input.data();
    const SketchPointArray& outputData = output.data();
    for (; i < numInputPoints; ++i) {
        Int j = outputData.length() - 1;
        const SketchPoint& lastOutputPoint = outputData.getUnchecked(j);
        const SketchPoint& inputPoint = inputData.getUnchecked(i);
        if (squaredDistance(inputPoint, lastOutputPoint) > ds2) {
            // Non-duplicate found => add it the the output.
            // (Note: the condition above is always true if ds2 = -1.
            output.append(inputPoint);
            lastNonDuplicateInputIndex = i;
            // Check whether adding this the output means that the previous
            // output point it now stable
            if (i < input.numStablePoints()) {
                startInputIndex_ = i;
                newNumStablePoints = j + 1;
            }
        }
        else {
            // Duplicate found. Get a mutable reference to the last output point.
            SketchPoint& duplicate = output.at(j);

            // Keep the largest pressure/width
            if (inputPoint.pressure() > duplicate.pressure()) {
                duplicate.setPressure(inputPoint.pressure());
                duplicate.setWidth(inputPoint.width());
            }
        }
    }

    // If the output has at least two points ([..., p, q]), change the position
    // and timestamp of the last output point q to be the same as the input
    // point r, among all input points merged into q, whose position is further
    // away from the second-last output point p.
    //
    Int numOutputPoints = output.length();
    if (numOutputPoints >= 2) {
        const SketchPoint& p = outputData.getUnchecked(numOutputPoints - 2);
        SketchPoint& q = output.at(numOutputPoints - 1);
        for (i = numInputPoints - 1; i >= lastNonDuplicateInputIndex + 1; --i) {
            const SketchPoint& r = inputData.getUnchecked(i);
            if (squaredDistance(r, p) > squaredDistance(q, p)) {
                q.setPosition(r.position());
                q.setTimestamp(r.timestamp());
            }
        }
    }

    output.updateChordLengths();
    output.setNumStablePoints(newNumStablePoints);
}

namespace {

bool clampMin_(double k, SketchPoint& p, const SketchPoint& minLimitor, double ds) {
    double minWidth = minLimitor.width() - k * ds;
    if (p.width() < minWidth) {
        p.setWidth(minWidth);
        return true;
    }
    return false;
}

void clampMax_(double k, SketchPoint& p, const SketchPoint& maxLimitor, double ds) {
    double maxWidth = maxLimitor.width() + k * ds;
    if (p.width() > maxWidth) {
        p.setWidth(maxWidth);
    }
}

// Clamps a point p based on a minLimitor before p.
// Returns whether p was widened according to minLimitor.
//
bool clampMinForward(double k, SketchPoint& p, const SketchPoint& minLimitor) {
    double dsMinLimitor = p.s() - minLimitor.s();
    return clampMin_(k, p, minLimitor, dsMinLimitor);
}

// Clamps a point p based on a minLimitor after p.
// Returns whether p was widened according to minLimitor.
//
bool clampMinBackward(double k, SketchPoint& p, const SketchPoint& minLimitor) {
    double dsMinLimitor = minLimitor.s() - p.s();
    return clampMin_(k, p, minLimitor, dsMinLimitor);
}

// Clamps a point p based on a maxLimitor before p.
//
void clampMaxForward(double k, SketchPoint& p, const SketchPoint& maxLimitor) {
    double dsMaxLimitor = p.s() - maxLimitor.s();
    clampMax_(k, p, maxLimitor, dsMaxLimitor);
}

// Clamps a point p based on a minLimitor and maxLimitor before p.
// Returns whether p was widened according to minLimitor.
//
bool clampMinMaxForward(
    double k,
    SketchPoint& p,
    const SketchPoint& minLimitor,
    const SketchPoint& maxLimitor) {

    if (clampMinForward(k, p, minLimitor)) {
        return true;
    }
    else {
        clampMaxForward(k, p, maxLimitor);
        return false;
    }
}

// Ensures that |dw/ds| <= k (e.g., k = 0.5).
//
// Importantly, we should have at least |dw/ds| <= 1, otherwise in the current
// tesselation model with round caps, it causes the following ugly artifact:
//
// Mouse move #123:   (pos = (100, 0), width = 3)
//
// -------_
//         =
//      +   |
//         =
// -------'
//
// Mouse move #124:   (pos = (101, 0), width = 1)
//
// -------   <- Ugly temporal disontinuity (previously existing geometry disappears)
//        .  <- Ugly geometric discontinuity (prone to cusps, etc.)
//       + |
//        '
// -------
//
// The idea is that what users see should be as close as possible as the
// "integral of disks" interpretation of a brush stroke. With a physical round
// paint brush, if you push the brush more then it creates a bigger disk. If
// you then pull a little without moving lateraly, then it doesn't remove what
// was previously already painted.
//
// Algorithm pseudo-code when applied to a global list of points:
//
// 1. Sort samples by width in a list
// 2. While the list isn't empty:
//    a. Pop sample with largest width
//    b. Modify the width of its two siblings to enforce |dw/ds| <= k
//    c. Update the location of the two siblings in the sorted list to keep it sorted
//
// Unfortunately, the above algorithm have global effect: adding one point with
// very large width might increase the width of all previous points. This is
// undesirable for performance and user-predictibility, as we want to keep the
// "unstable points" part of the sketched curve as small as possible.
// Therefore, in the implementation below, we only allow for a given point to
// affect `windowSize` points before itself.
//
void applyWidthRoughnessLimitor(
    double k,
    Int windowSize,
    const SketchPoint* lastStablePoint, // nullptr if no stable points
    core::Span<SketchPoint> unstablePoints) {

    if (unstablePoints.isEmpty()) {
        return;
    }

    // Apply width-limitor to first unstable point.
    //
    const SketchPoint* minLimitor = lastStablePoint;
    const SketchPoint* maxLimitor = lastStablePoint;
    if (lastStablePoint) {
        clampMinMaxForward(k, unstablePoints[0], *minLimitor, *maxLimitor);
    }

    // Apply width-limitor to subsequent unstable points.
    //
    for (Int i = 1; i < unstablePoints.length(); ++i) {

        //                   window size = 3    (each point influences up to
        //                <------------------|    3 points before itself)
        //
        //          p[windowStart]   p[i-1] p[i]
        // x-----x--------x--------x----x----x
        //   maxLimitor
        //
        Int windowStart = i - windowSize;
        if (windowStart > 0) {
            maxLimitor = &unstablePoints[windowStart - 1];
        }
        else {
            windowStart = 0;
            // maxLimitor == lastStablePoint
        }
        SketchPoint& p = unstablePoints[i];

        // Widen current point p[i] based on p[windowStart - 1]
        // Shorten current point p[i] based on p[i - 1]
        minLimitor = &unstablePoints[i - 1];
        bool widened = maxLimitor ? clampMinMaxForward(k, p, *minLimitor, *maxLimitor)
                                  : clampMinForward(k, p, *minLimitor);

        // Widen previous points within window if necessary.
        // Note: whenever a point is not itself widened, we know that previous
        // points will not be widened either, so we can skip computation.
        if (!widened) {
            for (Int j = i - 1; j >= windowStart; --j) {
                if (!clampMinBackward(k, unstablePoints[j], p)) {
                    break;
                }
            }
        }
    }
}

// cubicEaseInOut(t)
//       ^
//     1 |   .-
//       |_.´
//     0 +------> t
//       0    1
//
double cubicEaseInOut(double t) {
    double t2 = t * t;
    return -2 * t * t2 + 3 * t2;
}

// Fixup first/last width: on many tablets, they tend to go back to zero or
// very small values way too fast. So we limit them to not be smaller than a
// linear interpolation of the previous two values.
//
void improveEndWidths(core::DoubleArray& widths, Int unstableIndexStart) {
    Int numPoints = widths.length();
    double minWidthRatio = 1.0;
    if (numPoints < 2) {
        // Nothing to do
    }
    else if (numPoints == 2) {
        double w0 = widths.getUnchecked(0);
        double w1 = widths.getUnchecked(1);
        if (w0 < minWidthRatio * w1) {
            w0 = minWidthRatio * w1;
        }
        else if (w1 < minWidthRatio * w0) {
            w1 = minWidthRatio * w0;
        }
        if (unstableIndexStart == 0) {
            widths.getUnchecked(0) = w0;
        }
        if (unstableIndexStart < numPoints) {
            widths.getUnchecked(1) = w1;
        }
    }
    else {
        // Fixup start point
        if (unstableIndexStart == 0) {
            double w0 = widths.getUnchecked(0);
            double w1 = widths.getUnchecked(1);
            double w2 = widths.getUnchecked(2);
            double w0Min = minWidthRatio * (std::min)(w1, w1 + (w1 - w2));
            if (w0 < w0Min) {
                widths.getUnchecked(0) = w0Min;
            }
        }
        // Fixup end point
        if (unstableIndexStart < numPoints) {
            Int i0 = numPoints - 1;
            Int i1 = numPoints - 2;
            Int i2 = numPoints - 3;
            double w0 = widths.getUnchecked(i0);
            double w1 = widths.getUnchecked(i1);
            double w2 = widths.getUnchecked(i2);
            double w0Min = minWidthRatio * (std::min)(w1, w1 + (w1 - w2));
            if (w0 < w0Min) {
                widths.getUnchecked(i0) = w0Min;
            }
        }
    }
}

} // namespace

SmoothingPass::SmoothingPass()
    : SketchPass() {
}

SmoothingPass::SmoothingPass(const SmoothingSettings& settings)
    : SketchPass()
    , settings_(settings) {
}

void SmoothingPass::setSettings(const SmoothingSettings& settings) {
    checkCanChangeSettings(*this);
    settings_ = settings;
}

void SmoothingPass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    const SketchPointArray& inputPoints = input.data();
    Int numPoints = inputPoints.length();
    Int oldNumStablePoints = output.numStablePoints();
    if (numPoints == oldNumStablePoints) {
        return;
    }

    // Keep our stable points, fill the rest with the input points.
    Int unstableIndexStart = oldNumStablePoints;
    output.resize(oldNumStablePoints);
    output.extend(input.begin() + unstableIndexStart, input.end());

    Int instabilityDelta = 0;

    Int pointsSmoothingLevel = settings_.lineSmoothing();
    if (pointsSmoothingLevel > 0 && numPoints >= 3) {
        Int iMin = std::max<Int>(1, unstableIndexStart); // Do not move first point
        Int iMax = numPoints - 2;                        // Do not move last point
        for (Int i = iMin; i <= iMax; ++i) {

            // Reduce window size to ensure it stays within the index range
            Int l = pointsSmoothingLevel;
            if (i - l < 0) {
                l = i;
            }
            if (i + l > numPoints - 1) {
                l = numPoints - 1 - i;
            }
            VGC_ASSERT(l > 0);
            Int jMin = i - l;
            Int jMax = i + l;
            VGC_ASSERT(jMin >= 0 && jMax <= numPoints - 1);

            // Compute weighted average
            double du = 1.0 / (l + 1);
            geometry::Vec2d sumPositions = inputPoints.getUnchecked(i).position();
            double sumWeights = 1;
            for (Int j = jMin; j < i; ++j) {
                double u = 1.0 - (i - j) * du;
                double weight = cubicEaseInOut(u);
                sumPositions += weight * inputPoints.getUnchecked(j).position();
                sumWeights += weight;
            }
            for (Int j = i + 1; j <= jMax; ++j) {
                double u = 1.0 - (j - i) * du;
                double weight = cubicEaseInOut(u);
                sumPositions += weight * inputPoints.getUnchecked(j).position();
                sumWeights += weight;
            }
            output.at(i).setPosition(sumPositions / sumWeights);
        }
        instabilityDelta = std::max<Int>(instabilityDelta, pointsSmoothingLevel);
    }

    // Copy widths to a temporary buffer so that the smooth pass can use as input
    // the result of the improveEndWidths() method.
    //
    widthsBuffer_.clear();
    for (const auto& p : input) {
        widthsBuffer_.append(p.width());
    }

    // Improve the first/last widths.
    //
    Int widthInstabilityDelta = 0;
    if (settings_.improveEndWidths()) {
        improveEndWidths(widthsBuffer_, unstableIndexStart);

        // If width smoothing is disabled, copy the change to the output now
        if (settings_.widthSmoothing() <= 0 && numPoints > 0) {
            if (unstableIndexStart == 0) {
                output.at(0).setWidth(widthsBuffer_[0]);
            }
            if (unstableIndexStart < numPoints) {
                output.at(numPoints - 1).setWidth(widthsBuffer_[numPoints - 1]);
            }
        }

        // Instability caused by the first point: unless the first 3 input
        // points are stable, then the first output point is not stable.
        if (input.numStablePoints() < 3) {
            widthInstabilityDelta =
                std::max<Int>(widthInstabilityDelta, input.numStablePoints());
        }

        // Instability caused by the last point: no effect if the last input
        // was already unstable, otherwise adds an instability of 1 since
        // the output "improved last width" might get its original width back
        // if a new input point is added.
        if (input.numStablePoints() == numPoints) {
            widthInstabilityDelta += 1;
        }
    }

    // Smooth width.
    //
    // This is different as smoothing points since we don't need to keep
    // the first/last width unchanged.
    //
    // Note that while using a level larger than the number of points is
    // possible, it tends to create flickering for small strokes, due to the
    // last few points having much smaller width, and being taken into account
    // "too much", reducing the width of the first points.
    //
    //   Before adding        After adding
    //   the last point:      the last point:
    //
    //    -----------
    //   |           |        --------------
    //  |             |      |              |
    //   |           |        --------------
    //    ------------
    //
    // This could be solved by using something smarter than averaged-based
    // smoothing, e.g., quadratic fitting.
    //

    Int widthSmoothingLevel = settings_.widthSmoothing();
    if (widthSmoothingLevel > numPoints) {
        widthSmoothingLevel = numPoints;
    }
    if (widthSmoothingLevel > 0) {
        double du = 1.0 / (widthSmoothingLevel + 1);
        for (Int i = unstableIndexStart; i < numPoints; ++i) {
            double oldWidth = widthsBuffer_.getUnchecked(i);
            double sumWidths = oldWidth;
            double sumWeights = 1;
            Int jMin = std::max<Int>(i - widthSmoothingLevel, 0);
            for (Int j = jMin; j < i; ++j) {
                double u = 1.0 - (i - j) * du;
                double weight = cubicEaseInOut(u);
                sumWidths += weight * widthsBuffer_.getUnchecked(j);
                sumWeights += weight;
            }
            Int jMax = std::min<Int>(i + widthSmoothingLevel, numPoints - 1);
            for (Int j = i + 1; j <= jMax; ++j) {
                double u = 1.0 - (j - i) * du;
                double weight = cubicEaseInOut(u);
                sumWidths += weight * widthsBuffer_.getUnchecked(j);
                sumWeights += weight;
            }
            // Set the value only if different enough from the input,to prevent
            // rounding errors to appear when the width is actually constant.
            double newWidth = sumWidths / sumWeights;
            if (!core::isClose(newWidth, oldWidth)) {
                output.at(i).setWidth(newWidth);
            }
        }
        widthInstabilityDelta += widthSmoothingLevel;
    }
    instabilityDelta = std::max<Int>(instabilityDelta, widthInstabilityDelta);

    // compute chord lengths
    output.updateChordLengths();

    // Width limitor
    double widthRoughness = settings_.widthSlopeLimit();
    Int roughnessLimitorWindowSize = 3;
    const SketchPoint* lastStablePoint =
        oldNumStablePoints == 0 ? nullptr : &output[oldNumStablePoints - 1];
    applyWidthRoughnessLimitor(
        widthRoughness,
        roughnessLimitorWindowSize,
        lastStablePoint,
        output.unstablePoints());
    instabilityDelta += roughnessLimitorWindowSize;

    output.setNumStablePoints(
        std::max<Int>(0, input.numStablePoints() - instabilityDelta));
}

namespace {

// This is a variant of Douglas-Peucker designed to dequantize mouse inputs
// from integer to float coordinates.
//
// To this end, the distance test checks if the current line segment AB passes
// through all pixel squares of the samples in the interval. We call
// `threshold` the minimal distance between AB and the pixel center such that
// AB does not passes through the pixel square.
//
//     ---------------
//    |               |
//    |     pixel     |             threshold = distance(pixelCenter, A'B'),
//    |     center    |         B'  where A'B' is a line parallel to AB and
//    |       x       |       '     touching the pixel square.
//    |               |     '
//    |               |   '
//    |               | '
//     ---------------'
//                  '
//                ' A'
//
// This threshold only depends on the angle AB. If AB is perfectly horizontal
// or vertical, the threshold is equal to 0.5. If AB is at 45°, the threshold
// is equal to sqrt(2)/2 (the half-diagonal).
//
// We then scale this threshold is scaled with the given `thresholdCoefficient`
// and add the given `tolerance`.
//
// If the current segment does not pass the test, then the farthest samples is
// selected for the next iteration.
//
// Finally, it is possible to provide an `offset` to the algorithm. This is
// used to slightly move the position of selected samples towards the segment
// AB (by offset * threshold), which tends to give nicer results with an offset
// value of around 0.8.
//
Int douglasPeucker(
    core::Span<SketchPoint> points,
    core::IntArray& indices,
    Int intervalStart,
    double thresholdCoefficient,
    double offset,
    double tolerance = 0) {

    Int i = intervalStart;
    Int endIndex = indices[i + 1];
    while (indices[i] != endIndex) {

        // Get line AB. Fast discard if AB too small.
        Int iA = indices[i];
        Int iB = indices[i + 1];
        geometry::Vec2d a = points[iA].position();
        geometry::Vec2d b = points[iB].position();
        geometry::Vec2d ab = b - a;
        double abLen = ab.length();
        if (abLen < core::epsilon) {
            ++i;
            continue;
        }

        // Compute `threshold`
        constexpr double sqrtOf2 = 1.4142135623730950488017;
        double abMaxNormalizedAbsoluteCoord =
            (std::max)(std::abs(ab.x()), std::abs(ab.y())) / abLen;
        double abAngleWithClosestAxis = std::acos(abMaxNormalizedAbsoluteCoord);
        double threshold =
            std::cos(core::pi / 4 - abAngleWithClosestAxis) * (sqrtOf2 / 2);

        // Apply threshold coefficient and additive tolerance
        double adjustedThreshold = thresholdCoefficient * threshold + tolerance;

        // Compute which sample between A and B is furthest from the line AB
        double maxDist = 0;
        Int farthestPointSide = 0;
        Int farthestPointIndex = -1;
        for (Int j = iA + 1; j < iB; ++j) {
            geometry::Vec2d p = points[j].position();
            geometry::Vec2d ap = p - a;
            double dist = ab.det(ap) / abLen;
            // ┌─── x
            // │    ↑ side 1
            // │ A ───→ B
            // y    ↓ side 0
            Int side = 0;
            if (dist < 0) {
                dist = -dist;
                side = 1;
            }
            if (dist > maxDist) {
                maxDist = dist;
                farthestPointSide = side;
                farthestPointIndex = j;
            }
        }

        // If the furthest point is too far from AB, then recurse.
        // Otherwise, stop the recursion and move one to the next segment.
        if (maxDist > adjustedThreshold) {

            // Add sample to the list of selected samples
            indices.insert(i + 1, farthestPointIndex);

            // Move the position of the selected sample slightly towards AB
            constexpr bool isMoveEnabled = true;
            if (isMoveEnabled) {
                geometry::Vec2d n = ab.orthogonalized() / abLen;
                if (farthestPointSide != 0) {
                    n = -n;
                }
                // TODO: scale delta based on some data to prevent shrinkage?
                double delta = offset * threshold;
                SketchPoint& p = points[farthestPointIndex];
                p.setPosition(p.position() - delta * n);
            }
        }
        else {
            ++i;
        }
    }
    return i;
}

} // namespace

void DouglasPeuckerPass::setSettings(const DouglasPeuckerSettings& settings) {
    checkCanChangeSettings(*this);
    settings_ = settings;
}

void DouglasPeuckerPass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    // A copy is required to make a mutable span, which the Douglas-Peuckert
    // algorithm needs (it modifies the points slightly).
    //
    SketchPointArray inputPoints = input.data();

    Int firstIndex = 0;
    Int lastIndex = inputPoints.length() - 1;
    core::IntArray indices = {firstIndex, lastIndex};
    Int intervalStart = 0;

    core::Span<SketchPoint> span = inputPoints;
    double thresholdCoefficient = 1.0;
    douglasPeucker(
        span, indices, intervalStart, thresholdCoefficient, settings_.offset());

    Int numSimplifiedPoints = indices.length();
    output.resize(numSimplifiedPoints);
    for (Int i = 0; i < numSimplifiedPoints; ++i) {
        output.at(i) = inputPoints[indices[i]];
    }

    output.updateChordLengths();

    // For now, for simplicity, we do not provide any stable points guarantees
    // and simply recompute the Douglas-Peuckert algorithm from scratch.
    //
    output.setNumStablePoints(0);
}

namespace {

// Sets the output to be a line segment from first to last input point,
// assuming the first output point is stable as soon as the first input point
// is stable too.
//
void setLineSegmentWithFixedEndpoints(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    if (!input.isEmpty()) {
        output.resize(2);
        if (output.numStablePoints() == 0) {
            output.at(0) = input.first();
        }
        output.at(1) = input.last();
        output.updateChordLengths();
        output.setNumStablePoints(input.numStablePoints() > 0 ? 1 : 0);
    }
}

// Sets the output to be a line segment from first to last input point,
// assuming the first output point is never stable.
//
void setLineSegmentWithFreeEndpoints(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    if (!input.isEmpty()) {
        output.resize(2);
        output.at(0) = input.first();
        output.at(1) = input.last();
        output.updateChordLengths();
        output.setNumStablePoints(0);
    }
}

// When fitting a curve with fixed endpoints, this handles the case where the
// input is "small" (only two points or less), in which case the output should
// simply be a line (or an empty array).
//
// Returns whether the input was indeed small and therefore handled.
//
bool handleSmallInputWithFixedEndpoints(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    if (input.length() <= 2) {
        setLineSegmentWithFixedEndpoints(input, output);
        return true;
    }
    else {
        return false;
    }
}

// When fitting a curve with free endpoints, this handles the case where the
// input is "small" (only two points or less), in which case the output should
// simply be a line (or an empty array).
//
// Returns whether the input was indeed small and therefore handled.
//
bool handleSmallInputWithFreeEndpoints(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    if (input.length() <= 2) {
        setLineSegmentWithFreeEndpoints(input, output);
        return true;
    }
    else {
        return false;
    }
}

} // namespace

void SingleLineSegmentWithFixedEndpointsPass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    setLineSegmentWithFixedEndpoints(input, output);
}

// This basically implements:
//
// [1] Graphics Gems 5: The Best Least-Squares Line Fit (Alciatore and Miranda 1995)
//
// The chapter above provides a proof that the line minimizing the squared
// orthogonal distances to the input points goes through the centroid, and
// derives a closed form formula for the direction of the line.
//
// I believe this is equivalent to computing the first principal component of
// the data after centering it around the centroid (see PCA / SVD methods).
//
void SingleLineSegmentWithFreeEndpointsPass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    if (handleSmallInputWithFreeEndpoints(input, output)) {
        return;
    }

    // Compute centroid
    Int numPoints = input.length();
    geometry::Vec2d centroid;
    for (const SketchPoint& p : input) {
        centroid += p.position();
    }
    centroid /= core::narrow_cast<double>(numPoints);

    // Compute a = sum(xi² - yi²) and b = sum(2 xi yi)
    //
    // These can be interpreted as the coefficients of a complex number z = a + ib
    // such that sqrt(z) is parallel to the best fit line. See:
    //
    // [2] https://en.wikipedia.org/wiki/Deming_regression#Orthogonal_regression
    //
    double a = 0;
    double b = 0;
    for (const SketchPoint& p : input) {
        geometry::Vec2d q = p.position() - centroid;
        a += q.x() * q.x() - q.y() * q.y();
        b += q.x() * q.y();
    }
    b *= 2;

    // Compute coefficients of the best fit line as Ax + By + C = 0
    //
    // Note: if b = 0, then the best fit line is perfectly horizontal or
    // vertical, and [1] actually fails to handle/discuss the vertical case.
    //
    // Example 1: input points = (1, 0), (-1, 0)
    // The equation provided in [1] gives:
    // a = 2       b = 0
    // A = b = 0   B = -a - sqrt(a² + b²) = -4    => OK (horizontal line)
    //
    // Example 1: input points = (0, 1), (0, -1)
    // The equation provided in [1] gives:
    // a = -2      b = 0
    // A = b = 0   B = -a - sqrt(a² + b²) = 0     => WRONG (we need A != 0)
    //
    // The vertical case corresponds to the special case "z is a negative real
    // number" when computing the square root of a complex number via the
    // method in https://math.stackexchange.com/a/44500, which is essentially
    // the geometric interpretation of the equations provided in [1].
    //
    double A = 1;
    double B = 0;
    if (b == 0) { // XXX or |b| < eps * |a| ? What's a good eps?
        if (a < 0) {
            // Intuition: sum(xi²) < sum(yi²) => vertical line
            A = 1;
            B = 0;
        }
        else {
            // Intuition: sum(xi²) > sum(yi²) => horizontal line
            A = 0;
            B = 1;
        }
        // Note: if b == 0 AND a == 0, this means there is a circular symmetry:
        // all lines passing through the centroid are equally good/bad.
    }
    else {
        A = b;
        B = -(a + std::sqrt(a * a + b * b));
    }
    // Note: C = - A * centroid.x() + B * centroid.y(), but we do not need it.

    // Initialize the output from the first and last input points.
    // This ensures that we already have the width and timestamps correct.
    //
    output.resize(2);
    SketchPoint& p0 = output.at(0);
    SketchPoint& p1 = output.at(1);
    output.at(0) = input.first();
    output.at(1) = input.last();

    // Find points further away from centroid along the line, and project them
    // on the line to define the two output positions
    //
    // Note: the normal / direction vector is non-null since we know that
    // (A, B) is either (1, 0), (0, 1), or (b, ...) with b != 0
    //
    geometry::Vec2d d(-B, A);
    double vMin = core::DoubleInfinity;
    double vMax = -core::DoubleInfinity;
    for (const SketchPoint& p : input) {
        geometry::Vec2d q = p.position() - centroid;
        double v = q.dot(d);
        if (v < vMin) {
            vMin = v;
        }
        if (v > vMax) {
            vMax = v;
        }
    }
    double l2inv = 1.0 / d.squaredLength();
    geometry::Vec2d pMin = centroid + l2inv * vMin * d;
    geometry::Vec2d pMax = centroid + l2inv * vMax * d;
    if ((p1.position() - p0.position()).dot(d) > 0) {
        p0.setPosition(pMin);
        p1.setPosition(pMax);
    }
    else {
        p0.setPosition(pMax);
        p1.setPosition(pMin);
    }

    output.updateChordLengths();
    output.setNumStablePoints(0);

    // TODO: better width than using the width of first and last point?
}

namespace {

// Input:  n positions P0, ..., Pn-1
//         n params    u0, ..., un-1
//
// Output: Quadratic Bezier control points (B0, B1, B2) that minimizes:
//
//   E = sum || B(ui) - Pi ||²
//
// where:
//
//   B(u) = (1 - u)² B0 + 2(1 - u)u B1 + u² B2
//
//   B0 = P0
//   B1 = (x, y) is the variable that we solve for
//   B2 = Pn-1
//
// How do we solve for B1?
//
// The minimum of E is reached when dE/dx = 0 and dE/dy = 0
//
// dE/dx = sum d/dx(|| B(ui) - Pi ||²)
//       = sum 2 (B(ui) - Pi) ⋅ d/dx(B(ui) - Pi)    (dot product)
//
// d/dx(B(ui) - Pi) = d/dx(B(ui)) - d/dx(Pi)
//                  = d/dx(B(ui))              (since Pi is a constant)
//                  = (1 - ui)² d(B0)/dx + 2(1 - ui)ui d(B1)/dx + ui² d(B2)/dx
//                              ^^^^^^^^               ^^^^^^^^       ^^^^^^^^
//                              = (0, 0)               = (1, 0)       = (0, 0)
//
// So with the notations:
//   a0i = (1 - ui)²
//   a1i = 2(1 - ui)ui
//   a2i = ui²
//
// We have:
//
// d/dx(B(ui) - Pi) = (a1i, 0)
//
// And in the dot product (B(ui) - Pi) ⋅ d/dx(B(ui) - Pi), only the X-component
// is non-null and we get:
//
// dE/dx = sum 2 (B(ui)x - Pix) a1i
//       = sum 2 (a0i B0x + a1i x + a2i B2x - Pix) a1i
//
// Therefore,
//
// dE/dx = 0 <=> sum (a0i B0x + a1i x + a2i B2x - Pix) a1i = 0
//           <=> x * sum (a1i²) = sum (Pix - a0i B0x - a2i B2x) a1i
//
// So we get x = sum (Pix - a0i B0x - a2i B2x) a1i / sum (a1i²)
//
// We get a similar result for y, so in the end:
//
//          sum (Pi - a0i B0 - a2i B2) a1i
// (x, y) = ------------------------------
//                  sum (a1i²)
//
geometry::QuadraticBezier2d quadraticFitWithFixedEndpoints(
    core::ConstSpan<geometry::Vec2d> positions,
    core::ConstSpan<double> params) {

    Int n = positions.length();
    VGC_ASSERT(positions.length() == params.length());
    VGC_ASSERT(n > 0);

    const geometry::Vec2d& B0 = positions.first();
    if (n == 1) {
        return geometry::QuadraticBezier2d::point(B0);
    }

    const geometry::Vec2d& B2 = positions.last();
    if (n == 2) {
        return geometry::QuadraticBezier2d::lineSegment(B0, B2);
    }

    // Initialize numerator and denominator
    geometry::Vec2d numerator;
    double denominator = 0;

    // Iterate over all points except the first and last and accumulate
    // the terms in the numerator and denominator
    for (Int i = 1; i < n - 1; ++i) {
        geometry::Vec2d p = positions.getUnchecked(i);
        double u = params.getUnchecked(i);
        double v = 1 - u;
        double a0 = v * v;
        double a1 = 2 * v * u;
        double a2 = u * u;
        numerator += (p - a0 * B0 - a2 * B2) * a1;
        denominator += a1 * a1;
    }

    // Compute B1
    if (denominator > 0) {
        geometry::Vec2d B1 = numerator / denominator;
        return geometry::QuadraticBezier2d(B0, B1, B2);
    }
    else {
        // This means that a1 = 0 for all i, so (ui = 0) or (ui = 1) for all i.
        // It's basically bad input, and it's reasonable to fallback to a line segment.
        return geometry::QuadraticBezier2d::lineSegment(B0, B2);
    }
}

// Handles case n == 2 of quadraticFitWithFixedEndpointsAndStartTangent().
//
// We solve for B1 = B0 + aT with B1 on the bissection of B0-B2
//
//          o B1
//         /|
//        / |
//      _/  |
//    T /|  |
//     /    |
// B0 o-----+-----o B2
//          C
//
// Since B1 = B0 + aT, we have:
//
//     (B1 - B0) ⋅ (B2 - B0) = aT ⋅ (B2 - B0)
//
// But we also have:
//
//     (B1 - B0) ⋅ (B2 - B0) = (B1 - C + C - B0) ⋅ (B2 - B0)
//                           = (B1 - C) ⋅ (B2 - B0) + (C - B0) ⋅ (B2 - B0)
//                             ^^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^
//                                      0             0.5 (B2 - B0) ⋅ (B2 - B0)
//
//         || B2 - B0 ||²    numerator
//    a  = --------------- = ---------
//         2 T ⋅ (B2 - B0)   denominator
//
// But in order to avoid shooting to the star when T ⋅ (B2 - B0) is close to zero,
// we also enforce || aT || <= || B2 - B0 || so that we only output "reasonable"
// Bézier curves. This means:
//
//                                   a² || T ||² <= || B2 - B0 ||²
//                         (numerator)² || T ||² <= (denominator)² || B2 - B0 ||²
//                       || B2 - B0 ||² || T ||² <= (denominator)²
//
//
// Also, we want a >= 0, otherwise we would switch direction at B0 and
// the spline wouldn't be G1-continuous. So if a < 0, we do like when T
// is nearly perpendicular and simply use || aT || = || B2- B0 ||.
//
geometry::QuadraticBezier2d quadraticFitWithFixedEndpointsAndStartTangent_n2(
    const geometry::Vec2d& B0,
    const geometry::Vec2d& B2,
    const geometry::Vec2d& startTangent,
    double t2) {

    VGC_ASSERT(t2 > 0);

    geometry::Vec2d B0B2 = B2 - B0;
    double l2 = B0B2.squaredLength();
    if (l2 == 0) {
        // Special case B0 == B2
        return geometry::QuadraticBezier2d::point(B0);
    }
    double numerator = l2;
    double denominator = 2 * startTangent.dot(B0B2);
    if (denominator <= 0 || denominator * denominator <= l2 * t2) {
        // This handles all of these special cases:
        // 1. denominator == 0 (T perpendicular to B0-B2)
        // 2. denominator < 0  (T and (B2 - B0) facing opposite directions)
        // 3. || aT || >= || B2 - B0 || if we were using a = numerator / denominator
        //
        geometry::Vec2d B1 = B0 + l2 / t2 * startTangent;
        return geometry::QuadraticBezier2d(B0, B1, B2);
    }
    double a = numerator / denominator;
    geometry::Vec2d B1 = B0 + a * startTangent;
    return geometry::QuadraticBezier2d(B0, B1, B2);
}

// Input:  n positions P0, ..., Pn-1
//         n params    u0, ..., un-1
//         start tangent T
//
// Output: Quadratic Bezier control points (B0, B1, B2) that minimizes:
//
//   E = sum || B(ui) - Pi ||²
//
// where:
//
//   B(u) = (1 - u)² B0 + 2(1 - u)u B1 + u² B2
//
//   B0 = P0
//   B1 = P0 + a T, where a is the variable that we solve for
//   B2 = Pn-1
//
// With a similar method than quadraticFitWithFixedEndpoints() (without the
// given start tangent), we can develop dE/da using the fact that dB1/da = T,
// which gives the following closed form solution of dE/da = 0:
//
//     sum (Pi - (a0i + a1i) B0 - a2i B2) ⋅ a1i T
// a = ------------------------------------------
//                  T ⋅ T sum(a1i²)
//
geometry::QuadraticBezier2d quadraticFitWithFixedEndpointsAndStartTangent(
    core::ConstSpan<geometry::Vec2d> positions,
    core::ConstSpan<double> params,
    const geometry::Vec2d& startTangent) {

    Int n = positions.length();
    VGC_ASSERT(positions.length() == params.length());
    VGC_ASSERT(n > 0);

    const geometry::Vec2d& B0 = positions.first();
    if (n == 1) {
        return geometry::QuadraticBezier2d::point(B0);
    }

    const geometry::Vec2d& B2 = positions.last();
    double t2 = startTangent.squaredLength();
    if (t2 == 0) {
        // Special case T == 0
        return geometry::QuadraticBezier2d::lineSegment(B0, B2);
    }

    if (n == 2) {
        return quadraticFitWithFixedEndpointsAndStartTangent_n2(B0, B2, startTangent, t2);
    }

    // Initialize numerator and denominator
    double numerator = 0;
    double denominator = 0;

    // Iterate over all points except the first and last and accumulate
    // the terms in the numerator and denominator
    //
    //     sum (Pi - (a0i + a1i) B0 - a2i B2) ⋅ a1i T
    // a = ------------------------------------------
    //                  T ⋅ T sum(a1i²)
    //
    double B0T = B0.dot(startTangent);
    double B2T = B2.dot(startTangent);
    for (Int i = 1; i < n - 1; ++i) {
        geometry::Vec2d p = positions.getUnchecked(i);
        double u = params.getUnchecked(i);
        double v = 1 - u;
        double a0 = v * v;
        double a1 = 2 * v * u;
        double a2 = u * u;
        numerator += a1 * (p.dot(startTangent) - (a0 + a1) * B0T - a2 * B2T);
        denominator += a1 * a1;
    }
    denominator *= t2;

    // Compute B1
    if (denominator <= 0) {
        // This means that a1 = 0 for all i, so (ui = 0) or (ui = 1) for all i.
        // So it's like if the only information we have is B0, B2, and T.
        return quadraticFitWithFixedEndpointsAndStartTangent_n2(B0, B2, startTangent, t2);
    }

    double a = numerator / denominator;
    if (a <= 0) {
        // If a < 0, this means that the best fit is to go to the opposite
        // direction of T, but we don't want that. The best fit with a >= 0
        // would be a = 0 (since E(a) is a quadratic reaching its minimum
        // at a < 0), but we don't want that either as it would still not
        // be G1-continuous. So we arbitrarily output the Bézier satisfying
        // || B1 - B0 || = 0.1 * || B2 - B0 || in the direction of T.
        //
        geometry::Vec2d B0B2 = B2 - B0;
        double l2 = B0B2.squaredLength();
        if (l2 == 0) {
            // Special case B0 == B2
            return geometry::QuadraticBezier2d::point(B0);
        }
        constexpr double ratio = 0.1;
        geometry::Vec2d B1 = B0 + ratio * l2 / t2 * startTangent;
        return geometry::QuadraticBezier2d(B0, B1, B2);
    }

    // XXX: Do we also want to enforce || B1 - B0 || >= 0.1 * || B2 - B0 ||?
    //
    geometry::Vec2d B1 = B0 + a * startTangent;
    return geometry::QuadraticBezier2d(B0, B1, B2);
}

// This version simply uses one Newton-Raphson step, which can be unstable and
// make it worse if we are unlucky.
//
// Indeed, we are trying to find the root of a cubic, and if the current param
// is near a maximum/minimum of the cubic, then the Newton-Raphson step may
// send it very far.
//
//
// ^
// |
// |        If we start here and intersect the tangent with the X axis
// |           v
// |     .  +  .
// |   +          +         We end up here, which is worse than where we started
// |                +                    v
// +-----------------+-------------------------->
//                    +
//
// Other versions (optimizeParameters2() and optimizeParameters3()) are more
// accurate solvers that actually find solutions minimizing || B(u) - P ||, via
// global search or closed form analysis followed by multiple Newton-Raphson
// steps.
//
[[maybe_unused]] void optimizeParameters1(
    const geometry::QuadraticBezier2d& bezier,
    core::ConstSpan<geometry::Vec2d> positions,
    core::Span<double> params) {

    Int n = positions.length();
    VGC_ASSERT(positions.length() == params.length());

    // The second derivative of a quadratic does not actually depend on u, so
    // we compute it outside the loop.
    //
    geometry::Vec2d b2 = bezier.evalSecondDerivative(0);

    // For each parameter u, compute a better parameter by
    // doing one Newton-Raphson iteration:
    //
    //   u = u - f(u) / f'(u)
    //
    // with:
    //
    //   f(u) = 0.5 * d/du || B(u) - P ||²
    //        = (B(u) - P) ⋅ dB/du
    //
    //   f'(u) = dB/du ⋅ dB/du + (B(u) - P) ⋅ d²B/du
    //
    //
    for (Int i = 1; i < n - 1; ++i) {
        geometry::Vec2d p = positions.getUnchecked(i);
        double& u = params.getUnchecked(i);
        geometry::Vec2d b1;
        geometry::Vec2d b0 = bezier.eval(u, b1);
        double numerator = (b0 - p).dot(b1);
        double denominator = b1.dot(b1) + (b0 - p).dot(b2);
        if (std::abs(denominator) > 0) {
            u = u - numerator / denominator;
        }
        // Enforce increasing u-parameters
        double uBefore = params.getUnchecked(i - 1);
        u = core::clamp(u, uBefore, 1.0);
    }
}

// This version ignores the input params, and instead directly find which param
// is the global minimum for each given position. This improves a lot the
// results (but is slower) since it never gets stuck in a local extrema.

[[maybe_unused]] void optimizeParameters2(
    const geometry::QuadraticBezier2d& bezier,
    core::ConstSpan<geometry::Vec2d> positions,
    core::Span<double> params) {

    Int n = positions.length();
    VGC_ASSERT(positions.length() == params.length());

    // Evaluate uniform samples along the Bezier curve
    constexpr Int numUniformSamples = 256;
    constexpr double du = 1.0 / (numUniformSamples - 1);
    std::array<std::pair<double, geometry::Vec2d>, numUniformSamples> uniformSamples;
    for (Int k = 0; k < numUniformSamples; k++) {
        double u = du * k;
        uniformSamples[k] = {u, bezier.eval(u)};
    }

    geometry::Vec2d b2 = bezier.secondDerivative();

    for (Int i = 1; i < n - 1; ++i) {
        geometry::Vec2d p = positions.getUnchecked(i);

        // Find closest point among uniform samples and corresponding u
        double minDist = core::DoubleInfinity;
        double u = 0;
        for (auto [u_, q] : uniformSamples) {
            double dist = (q - p).squaredLength();
            if (dist < minDist) {
                minDist = dist;
                u = u_;
            }
        }

        // Perform several Newton-Raphson iterations from there
        const Int numIterations = 10;
        for (Int j = 0; j < numIterations; ++j) {
            geometry::Vec2d b1;
            geometry::Vec2d b0 = bezier.eval(u, b1);
            double numerator = (b0 - p).dot(b1);
            double denominator = b1.dot(b1) + (b0 - p).dot(b2);
            if (std::abs(denominator) > 0) {
                u = u - numerator / denominator;
            }
            else {
                break;
            }
        }

        // Enforce increasing u-parameters
        double uBefore = params.getUnchecked(i - 1);
        u = core::clamp(u, uBefore, 1.0);

        // Set the value in params
        params.getUnchecked(i) = u;
    }
}

// This version accurately detects the case where there are two local
// minima, and accurately computes the most appropriate one.
//
// It does not attempt to keep u-parameters increasing, since the results are
// already really good even when keeping potential switch-backs.
//
// Some explanations of the methods and notations:
//
// B(u) = (1-u)² B0 + 2(1-u)u B1 + u² B2
//      = au² + bu + c
//
// with:
//  a = B0 - 2 B1 + B2
//  b = -2 B0 + 2 B1
//  c = B0
//
// therefore:
//  B'(u) = 2au + b
//  B''(u) = 2a
//
// We want to find the local minima of || B(u) - P || for each input point P.
//
// These satisfy:
//   f(u) = 0
//   f'(u) >= 0
//
// with:
//   f(u) = 0.5 * d/du || B(u) - P ||²
//        = (B(u) - P) B'(u)
//        = 2a²u³ + 3abu² + (b²+2(c-P)a)u + (c-P)b
//
//   f'(u)  = 6a²u² + 6abu + (b²+2(c-P)a)
//   f''(u) = 12a²u + 6ab
//
// (where all vector-vector products are dot products)
//
// So f is a cubic polynomial with a positive u³ term.
// It has either one of two real solutions satisfying:
//
//   f(u) = 0
//   f'(u) >= 0
//
// The local extrema (if D >= 0) of f are:
//
//  f'(u) = 0  =>   u1, u2 = (-6ab ± sqrt(D))/12a²
//                  with D = (6ab)² - 4*6a²*(b²+2(c-P)a)
//                         = 36(ab)(ab) - 24a²(b²+2(c-P)a)
//                         = D1 + D2 * (c-P)a
//                  with h = ab/2 = a.dot(b/2) = a.dot(B1 - B0)
//                      D1 = 144h² - 24a²b²
//                      D2 = -48a²
//
// The inflexion point of f (= its point of rotational symmetry) is:
//
//  f''(u) = 0  =>  u = -ab / 2a²
//                    = -h / a²
//
// Note how the inflexion point does not depend on P! In fact, it can be proven
// that it corresponds to the maximum of curvature of B.
//
[[maybe_unused]] void optimizeParameters3(
    const geometry::QuadraticBezier2d& bezier,
    core::ConstSpan<geometry::Vec2d> positions,
    core::Span<double> params) {

    Int n = positions.length();
    VGC_ASSERT(positions.length() == params.length());

    const geometry::Vec2d& B0 = bezier.p0();
    const geometry::Vec2d& B1 = bezier.p1();
    const geometry::Vec2d& B2 = bezier.p2();

    geometry::Vec2d B0B1 = B1 - B0;
    geometry::Vec2d B1B2 = B2 - B1;
    geometry::Vec2d B0B2 = B2 - B0;

    geometry::Vec2d a = B1B2 - B0B1;
    //geometry::Vec2d b = 2 * B0B1;
    geometry::Vec2d c = B0;

    double a2 = a.squaredLength();
    double B0B22 = B0B2.squaredLength();

    constexpr double eps = 1e-12;
    if (a2 <= eps * B0B22) { // Important: `<=` handles case (a2 == 0 && B0B22 == 0)
        // => B0 - 2 B1 + B2 = 0 => B1 = 0.5 * (B0 + B1) => line segment
        //
        // In this case, since B(u) is actually a linear function, the initial
        // parameters should already be pretty good, but we still improve them
        // anyway by computing the projection to the line segment.
        //
        double l2 = B0B2.squaredLength();
        if (l2 <= 0) {
            // Segment reduced to point: cannot project, so we keep params as is.
            return;
        }
        for (Int i = 1; i < n - 1; ++i) {
            geometry::Vec2d p = positions.getUnchecked(i);
            if (l2 <= eps * std::abs((p - B0).dot(B0B2))) {
                // Segment basically reduced to a point (compared to ||B0-P||).
                // Projecting would be numerically instable, so we keep params as is.
                return;
            }
        }
        double l2Inv = 1.0 / l2;
        for (Int i = 1; i < n - 1; ++i) {
            geometry::Vec2d p = positions.getUnchecked(i);
            double u = (p - B0).dot(B0B2) * l2Inv;
            params.getUnchecked(i) = u;
        }
        return;
    }

    double h = a.dot(B0B1);
    double b2 = 4 * B0B1.dot(B0B1);
    double D1 = 144 * h * h - 24 * a2 * b2;
    double D2 = -48 * a2;
    double a2Inv = 1.0 / a2;
    double uInflexion = -h * a2Inv;
    geometry::Vec2d der2 = 2 * a; // == bezier.secondDerivative()

    // Lambda that evaluates f(u) for point P.
    //
    auto f = [=](double u, const geometry::Vec2d& p) {
        geometry::Vec2d der;
        geometry::Vec2d pos = bezier.eval(u, der);
        return (pos - p).dot(der);
    };

    // Lambda that computes Newton-Raphson iteration starting at u, and returns
    // the final result.
    //
    auto newtonRaphson = [=](double u, const geometry::Vec2d& p) {
        const Int maxIterations = 32;
        const double resolution = 1e-8;
        for (Int j = 0; j < maxIterations; ++j) {
            geometry::Vec2d der;
            geometry::Vec2d pos = bezier.eval(u, der);
            double numerator = (pos - p).dot(der);
            double denominator = der.dot(der) + (pos - p).dot(der2);
            double lastU = u;
            if (std::abs(denominator) > 0) {
                u = u - numerator / denominator;
            }
            else {
                // This is not supposed to happen since we enforce the initial
                // guess to be in stable interval where f'(u) > 0 in the whole
                // interval. If this happens anyway (numerical error?), we try
                // to recover by simply adding a small perturbation to u.
                //
                VGC_WARNING(
                    LogVgcToolsSketch, "Null derivative in Newton-Raphson iteration.");
                u += 0.1;
            }
            if (std::abs(lastU - u) < resolution) {
                break;
            }
            lastU = u;
        }
        return u;
    };

    for (Int i = 1; i < n - 1; ++i) {
        geometry::Vec2d p = positions.getUnchecked(i);
        double D = D1 + D2 * (c - p).dot(a);
        double u = 0;
        if (D <= 0) {
            // If D == 0:                         If D < 0:
            //
            // f'(uInflexion) = 0                 f'(u) > 0 everywhere
            // f'(u) > 0 everywhere else
            //
            //            |                               |
            //           /                               /
            //      .-o-'                               o  uInflexion
            //     /  uInflexion                      /
            //    |                                  |
            //
            // There is exactly one solution.
            //
            double A = f(uInflexion, p);
            if (A == 0) {
                u = uInflexion;
            }
            else if (A > 0) {
                u = newtonRaphson(uInflexion - 1, p); // (-inf, uInflexion) is stable
            }
            else {
                u = newtonRaphson(uInflexion + 1, p); // (uInflexion, inf) is stable
            }
        }
        else {
            //  uExtrema1
            //     v
            //   .--.  uInflexion
            //  '    o
            //        .__.'
            //         ^
            //      uExtrema2
            //
            double offset = (1.0 / 12.0) * std::sqrt(D) * a2Inv;
            double uExtrema1 = uInflexion - offset;
            double uExtrema2 = uInflexion + offset;
            if (f(uExtrema2, p) > 0) {               // no solution in (uExtrema1, inf)
                u = newtonRaphson(uExtrema1 - 1, p); // (-inf, uExtrema1) is stable
            }
            else if (f(uExtrema1, p) < 0) {          // no solution in (-inf, uExtrema2)
                u = newtonRaphson(uExtrema2 + 1, p); // (uExtrema2, inf) is stable
            }
            else {
                // There is one solution in (-inf, uExtrema1) and one in
                // (uExtrema2, inf).
                //
                // We pick the one that preserves which side of uInflexion we
                // are. This choice is very stable and leads to good results
                // because uInflexion does not depend on P, and input points
                // that are close to uInflexion are typically in the case where
                // there is only one solution anyway (D < 0).
                //
                double oldU = params.getUnchecked(i);
                if (oldU < uInflexion) {
                    u = newtonRaphson(uExtrema1 - 1, p);
                }
                else {
                    u = newtonRaphson(uExtrema2 + 1, p);
                }
            }
        }
        params.getUnchecked(i) = u;
    }
}

// Using this method at the end of a fit pass make it possible
// to visualize the computed parameters by simply taking the input
// points, and moving them to bezier(params[i]).
//
// You may want to call optimizeParameters() beforehand if you wish
// to visualize the output of optimizeParameters(), or not call it
// if you prefer to visualize the output of the least-squares fit.
//
[[maybe_unused]] void setOutputAsMovedInputPoints(
    const geometry::QuadraticBezier2d& bezier,
    core::ConstSpan<double> params,
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    Int n = input.length();
    VGC_ASSERT(params.length() == input.length());

    output.resize(1);
    output.reserve(n);
    if (output.numStablePoints() == 0) {
        output.at(0) = input.first();
    }
    const SketchPointArray& inputData = input.data();
    for (Int i = 1; i < n - 1; ++i) {
        SketchPoint p = inputData.getUnchecked(i);
        double u = params.getUnchecked(i);
        p.setPosition(bezier.eval(u));
        output.append(p);
    }
    output.append(input.last());
}

void addToOutputAsUniformParams(
    const geometry::QuadraticBezier2d& bezier,
    Int numOutputSegments,
    core::ConstSpan<double> params,
    const SketchPointBuffer& input,
    Int firstIndex,
    Int lastIndex,
    SketchPointBuffer& output) {

    VGC_ASSERT(firstIndex >= 0);
    VGC_ASSERT(firstIndex < input.length());
    VGC_ASSERT(lastIndex >= 0);
    VGC_ASSERT(lastIndex < input.length());

    Int n = lastIndex - firstIndex + 1;
    VGC_ASSERT(n > 0);
    VGC_ASSERT(params.length() == n);
    VGC_ASSERT(numOutputSegments >= 1);
    VGC_ASSERT(params.first() == 0.0);
    VGC_ASSERT(params.last() == 1.0);

    double du = 1.0 / numOutputSegments;

    // Note: we do not add a point at u=0 since it is expected to already be
    // present in the output (last point of previous Bézier)

    Int i = 0; // Invariant: 0 <= i < n-1 (so both i and i+1 are valid points)
    const SketchPointArray& inputData = input.data();
    for (Int j = 1; j < numOutputSegments; ++j) {

        // The parameter of the output sample
        double u = j * du;

        // Find pair (i, i+1) of input points such that params[i] <= u < params[i+1].
        //
        // Note that since:
        //
        // - 0 < u < 1
        // - params.first() == 0
        // - params.last() == 1
        // - params[k] <= params[k+1] for all k in [0..n-2]
        //
        // It is always possible to find such pair and preserve the invariant
        // i < n-1, since once we reach i = n-2, then the while condition is
        // always false since u < 1 and params[i+1] = params[n-1] = 1 so
        // u < params[i+1].
        //
        while (u >= params.getUnchecked(i + 1)) {
            ++i;
            VGC_ASSERT(i + 1 < n);
        }

        // Output point at bezier.eval(u), with all other params linearly
        // interpolated between input[i] and input[i+1]
        //
        // TODO: For the width/pressure, it would probably be better to take
        // the average of all input points between param `(j - 0.5) * du` and
        // param `(j + 0.5) * du`.
        //
        const SketchPoint p0 = inputData.getUnchecked(firstIndex + i);
        const SketchPoint p1 = inputData.getUnchecked(firstIndex + i + 1);
        double u0 = params.getUnchecked(i);
        double u1 = params.getUnchecked(i + 1);
        VGC_ASSERT(u0 < u1);
        SketchPoint p = core::fastLerp(p0, p1, (u - u0) / (u1 - u0));
        p.setPosition(bezier.eval(u));
        output.append(p);
    }
    output.append(inputData.getUnchecked(lastIndex));
}

[[maybe_unused]] void setOutputAsUniformParams(
    const geometry::QuadraticBezier2d& bezier,
    Int numOutputSegments,
    core::ConstSpan<double> params,
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    output.resize(1);
    if (output.numStablePoints() == 0) {
        output.at(0) = input.first();
    }
    Int firstIndex = 0;
    Int lastIndex = input.length() - 1;
    addToOutputAsUniformParams(
        bezier, numOutputSegments, params, input, firstIndex, lastIndex, output);
}

// Helper function for handling the base case of fits with or without tangents
//
std::optional<geometry::QuadraticBezier2d> quadraticFitCommon_(
    const SketchPointBuffer& input,
    Int firstIndex,
    Int lastIndex,
    geometry::Vec2dArray& positions,
    core::DoubleArray& params) {

    VGC_ASSERT(firstIndex >= 0);
    VGC_ASSERT(firstIndex < input.length());
    VGC_ASSERT(lastIndex >= 0);
    VGC_ASSERT(lastIndex < input.length());

    Int n = lastIndex - firstIndex + 1;
    VGC_ASSERT(n > 0);

    // Copy input positions and initialize params as normalized chord-length.
    positions.resize(0);
    params.resize(0);
    positions.reserve(n);
    params.reserve(n);
    const SketchPointArray& points = input.data();
    double s0 = points.getUnchecked(firstIndex).s();
    double totalChordLength = points.getUnchecked(lastIndex).s() - s0;
    double totalChordLengthInv = 0;
    if (totalChordLength > 0) {
        totalChordLengthInv = 1.0 / totalChordLength;
    }
    for (Int i = firstIndex; i <= lastIndex; ++i) {
        const SketchPoint& p = points.getUnchecked(i);
        positions.append(p.position());
        params.append((p.s() - s0) * totalChordLengthInv);
    }

    // Ensure first and last parameters are exactly 0 and 1, which we need as
    // precondition for setOutputAsUniformParams(). This might not already be
    // the case due to numerical errors, or in the degenerate case where
    // totalChordLength == 0.
    //
    params.first() = 0;
    params.last() = 1;

    // Handle trivial or degenerate cases
    const geometry::Vec2d& pFirst = positions.first();
    if (n == 1) {
        return geometry::QuadraticBezier2d::point(pFirst);
    }
    const geometry::Vec2d& pLast = positions.last();
    if (n == 2 || totalChordLengthInv == 0) {
        return geometry::QuadraticBezier2d::lineSegment(pFirst, pLast);
    }

    return std::nullopt;
}

// Computes the best quadratic fit for the `input` points between the
// `firstIndex` and `lastIndex`.
//
// After calling this function, positions is set to a copy of the input
// positions, and params is set to the parameters mapping the input points to
// the quadratic Bézier.
//
geometry::QuadraticBezier2d quadraticFitWithFixedEndpoints(
    const SketchPointBuffer& input,
    Int firstIndex,
    Int lastIndex,
    geometry::Vec2dArray& positions,
    core::DoubleArray& params) {

    if (auto res = quadraticFitCommon_(input, firstIndex, lastIndex, positions, params)) {
        return *res;
    }

    // Iteratively compute best fit with progressively better params
    geometry::QuadraticBezier2d bezier;
    constexpr Int numIterations = 4;
    for (Int k = 0; k < numIterations; ++k) {
        bezier = quadraticFitWithFixedEndpoints(positions, params);
        optimizeParameters3(bezier, positions, params);
    }
    return bezier;
}

geometry::QuadraticBezier2d quadraticFitWithFixedEndpoints(
    const SketchPointBuffer& input,
    Int firstIndex,
    Int lastIndex,
    detail::FitBuffer& buffer) {

    return quadraticFitWithFixedEndpoints(
        input, firstIndex, lastIndex, buffer.positions, buffer.params);
}

geometry::QuadraticBezier2d quadraticFitWithFixedEndpoints(
    const SketchPointBuffer& input,
    geometry::Vec2dArray& positions,
    core::DoubleArray& params) {

    return quadraticFitWithFixedEndpoints(
        input, 0, input.length() - 1, positions, params);
}

geometry::QuadraticBezier2d quadraticFitWithFixedEndpointsAndStartTangent(
    const SketchPointBuffer& input,
    Int firstIndex,
    Int lastIndex,
    geometry::Vec2dArray& positions,
    core::DoubleArray& params,
    const geometry::Vec2d& startTangent) {

    if (auto res = quadraticFitCommon_(input, firstIndex, lastIndex, positions, params)) {
        return *res;
    }

    // Iteratively compute best fit with progressively better params
    geometry::QuadraticBezier2d bezier;
    constexpr Int numIterations = 4;
    for (Int k = 0; k < numIterations; ++k) {
        bezier = quadraticFitWithFixedEndpointsAndStartTangent(
            positions, params, startTangent);
        optimizeParameters3(bezier, positions, params);
    }
    return bezier;
}

[[maybe_unused]] geometry::QuadraticBezier2d
quadraticFitWithFixedEndpointsAndStartTangent(
    const SketchPointBuffer& input,
    geometry::Vec2dArray& positions,
    core::DoubleArray& params,
    const geometry::Vec2d& startTangent) {

    return quadraticFitWithFixedEndpointsAndStartTangent(
        input, 0, input.length() - 1, positions, params, startTangent);
}

} // namespace

void SingleQuadraticSegmentWithFixedEndpointsPass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    if (handleSmallInputWithFixedEndpoints(input, output)) {
        return;
    }

    // Compute best quadratic fit
    geometry::QuadraticBezier2d bezier =
        quadraticFitWithFixedEndpoints(input, buffer_.positions, buffer_.params);

    // Compute output from fit
    constexpr bool outputAsMovedInputPoint = false;
    if (outputAsMovedInputPoint) {
        setOutputAsMovedInputPoints(bezier, buffer_.params, input, output);
    }
    else {
        constexpr Int numOutputSegments = 8;
        setOutputAsUniformParams(
            bezier, numOutputSegments, buffer_.params, input, output);
    }

    output.updateChordLengths();
    output.setNumStablePoints(input.numStablePoints() > 0 ? 1 : 0);
}

namespace experimental {

Int FitSplitStrategy::getSplitIndex( //
    Int firstIndex,
    Int lastIndex,
    Int furthestIndex) const {

    Int splitIndex = firstIndex;
    switch (type()) {
    case FitSplitType::Furthest:
        splitIndex = furthestIndex;
        break;
    case FitSplitType::RelativeToStart:
        splitIndex = firstIndex + offset();
        break;
    case FitSplitType::RelativeToEnd:
        splitIndex = lastIndex - offset();
        break;
    case FitSplitType::IndexRatio:
        splitIndex = core::ifloor<Int>(core::fastLerp(
            core::narrow_cast<double>(firstIndex),
            core::narrow_cast<double>(lastIndex),
            ratio()));
    }
    return core::clamp(splitIndex, firstIndex, lastIndex);
}

} // namespace experimental

namespace {

Int getSplitIndex(
    const experimental::FitSplitStrategy& strategy,
    const detail::BlendFitInfo& fit) {

    return strategy.getSplitIndex(
        fit.firstInputIndex, fit.lastInputIndex, fit.furthestIndex);
}

// Computes the largest distance squared between the input position and its
// corresponding point on the Bézier curve, excluding the endpoints.
//
// Returns this distance squared and the smallest index for which it is reached.
//
// Returns (distance = -1, index=0) if n <= 2, that is, if there are no interior
// points.
//
std::pair<double, Int> maxDistanceSquared(
    const geometry::QuadraticBezier2d& bezier,
    core::ConstSpan<geometry::Vec2d> positions,
    core::ConstSpan<double> params) {

    VGC_ASSERT(positions.length() == params.length());
    Int n = positions.length();

    double distance = -1;
    Int index = 0;
    for (Int i = 1; i < n - 1; ++i) {
        const geometry::Vec2d& p = positions.getUnchecked(i);
        double u = params.getUnchecked(i);
        double d = (p - bezier.eval(u)).squaredLength();
        if (d > distance) {
            distance = d;
            index = i;
        }
    }
    return {distance, index};
}

std::pair<double, Int> maxDistanceSquared(
    const geometry::QuadraticBezier2d& bezier,
    const detail::FitBuffer& buffer) {

    return maxDistanceSquared(bezier, buffer.positions, buffer.params);
}

struct RecursiveQuadraticFitData {
    const experimental::SplineFitSettings& settings;
    const SketchPointBuffer& input;
    SketchPointBuffer& output;
    geometry::Vec2dArray& positions;
    core::DoubleArray& params;
    core::Array<detail::SplineFitInfo>& info;
};

void recursiveQuadraticFit(
    RecursiveQuadraticFitData& d,
    Int firstInputIndex,
    Int lastInputIndex,
    bool splitLastGoodFitOnce) {

    // Compute the best quadratic fit of the input points between
    // `firstInputIndex` and `lastInputIndex` (included).
    //
    // If a quadratic fit has already been computed for the previous points,
    // then we use its end tangent as our start tangent to enforce
    // G1-continuity.
    //
    geometry::QuadraticBezier2d bezier;
    if (d.info.isEmpty()) {
        bezier = quadraticFitWithFixedEndpoints(
            d.input, firstInputIndex, lastInputIndex, d.positions, d.params);
    }
    else {
        const geometry::QuadraticBezier2d& lastBezier = d.info.last().bezier;
        geometry::Vec2d t = lastBezier.p2() - lastBezier.p1();
        bezier = quadraticFitWithFixedEndpointsAndStartTangent(
            d.input, firstInputIndex, lastInputIndex, d.positions, d.params, t);
    }

    // Compute the max distance squared between the input points and the Bézier fit,
    // and check whether it is within the chosen threshold.
    //
    double distanceThreshold = d.settings.distanceThreshold;
    double distanceSquaredThreshold = distanceThreshold * distanceThreshold;
    auto [distanceSquared, index] = maxDistanceSquared(bezier, d.positions, d.params);
    Int furthestIndex = index + firstInputIndex; // Convert to [0..numInputPoints-1]
    bool isWithinDistance = distanceSquared <= distanceSquaredThreshold;

    // Determine whether we should use the flatness threshold.
    //
    Int numInputPoints = lastInputIndex - firstInputIndex + 1;
    double flatnessThreshold = d.settings.flatnessThreshold;
    bool enableFlatnessThreshold =
        (flatnessThreshold >= 0)
        && (numInputPoints > d.settings.flatnessThresholdMinPoints);

    // If enabled, compute the square of the flatness, and compare it with the
    // square of the flatness threshold. Alternatively, we could directly
    // define the flatness to be the square of the current flatness, but then
    // choosing a flatness threshold would be less intuitive since the scale
    // would be quadratic instead of linear.
    //
    //                             o        --> 2x taller means 2x less flat
    //         o
    //   o           o       o           o
    //
    //   B0 = (0, 0)         B0 = (0, 0)
    //   B1 = (2, 1)         B1 = (2, 2)
    //   B2 = (4, 0)         B2 = (4, 0)
    //   flatness  = 1       flatness  = 0.5
    //   flatness² = 1       flatness² = 0.25
    //
    bool isWithinFlatness = true;
    if (enableFlatnessThreshold) {
        double flatnessSquaredThreshold = flatnessThreshold * flatnessThreshold;
        double flatnessSquared = core::DoubleInfinity;
        flatnessThreshold *= flatnessThreshold;
        double der2 = bezier.secondDerivative().squaredLength();
        double l2 = (bezier.p2() - bezier.p0()).squaredLength();
        if (der2 > 0) {
            flatnessSquared = l2 / der2;
        }
        else {
            // Perfectly flat => flatnessSquared = core::DoubleInfinity;
        }
        isWithinFlatness = flatnessSquared >= flatnessSquaredThreshold;
    }

    bool isGoodFit = isWithinDistance && isWithinFlatness;
    bool cannotSplit = (distanceSquared == -1);
    if (cannotSplit || (isGoodFit && !splitLastGoodFitOnce)) {
        addToOutputAsUniformParams(
            bezier,
            d.settings.numOutputPointsPerBezier,
            d.params,
            d.input,
            firstInputIndex,
            lastInputIndex,
            d.output);
        Int lastOutputIndex = d.output.length() - 1;
        detail::SplineFitInfo info{lastInputIndex, lastOutputIndex, bezier};
        d.info.append(info);
    }
    else {
        // Compute where to split based on the chosen SplitStategy
        Int splitIndex = d.settings.splitStrategy.getSplitIndex(
            firstInputIndex, lastInputIndex, furthestIndex);

        // Recursively call two fits on both sides of the split index
        bool newSplitLastGoodFitOnce = splitLastGoodFitOnce && !isGoodFit;
        recursiveQuadraticFit(d, firstInputIndex, splitIndex, false);
        recursiveQuadraticFit(d, splitIndex, lastInputIndex, newSplitLastGoodFitOnce);
    }
}

} // namespace

QuadraticSplinePass::QuadraticSplinePass()
    : SketchPass()
    , settings_() {
}

QuadraticSplinePass::QuadraticSplinePass(const experimental::SplineFitSettings& settings)
    : SketchPass()
    , settings_(settings) {
}

void QuadraticSplinePass::doReset() {
    info_.clear();
}

void QuadraticSplinePass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    if (handleSmallInputWithFixedEndpoints(input, output)) {
        return;
    }

    // Remove all previously unstable output points and Bézier fits.
    //
    Int oldNumStablePoints = output.numStablePoints();
    output.resize(oldNumStablePoints);
    while (!info_.isEmpty() && info_.last().lastOutputIndex >= oldNumStablePoints) {
        info_.pop();
    }
    VGC_ASSERT(info_.isEmpty() || info_.last().lastOutputIndex == oldNumStablePoints - 1);

    // Add the first output point unless it was already stable
    if (oldNumStablePoints == 0) {
        output.append(input.first());
    }

    RecursiveQuadraticFitData data = {
        settings_, input, output, buffer_.positions, buffer_.params, info_};
    Int firstIndex = info_.isEmpty() ? 0 : info_.last().lastInputIndex;
    Int lastIndex = input.length() - 1;
    bool splitLastGoodFitOnce = settings_.splitLastGoodFitOnce;
    if (lastIndex > firstIndex) {
        recursiveQuadraticFit(data, firstIndex, lastIndex, splitLastGoodFitOnce);
    }

    output.updateChordLengths();

    // Determine the new number of stable output points and Bézier fits, by
    // iterating backward over all Bézier fits.
    //
    // We start at i = numFits - 2 (or -3) because the last fit (or the last
    // two fits) is always considered unstable, even if all the input points
    // were stable.
    //
    Int newNumStablePoints = 0;
    if (input.numStablePoints() > 0) {
        newNumStablePoints = 1;
    }
    Int firstPossiblyStableFit =
        splitLastGoodFitOnce ? info_.length() - 3 : info_.length() - 2;
    for (Int i = firstPossiblyStableFit; i >= 0; --i) {
        const detail::SplineFitInfo& info = info_.getUnchecked(i);
        if (info.lastInputIndex < input.numStablePoints()) {
            newNumStablePoints = info.lastOutputIndex + 1;
            break;
        }
    }
    output.setNumStablePoints(newNumStablePoints);
}

namespace {

core::StringId debugDrawId("QuadraticBlend");

void debugDraw(const geometry::Mat3d& transform, canvas::DebugDrawFunction function) {
    auto viewMatrix = geometry::Mat4f::fromTransform(transform);
    canvas::debugDraw(debugDrawId, [function, viewMatrix](graphics::Engine* engine) {
        engine->pushViewMatrix(engine->viewMatrix() * viewMatrix);
        function(engine);
        engine->popViewMatrix();
    });
}

void debugDrawClear() {
    canvas::debugDrawClear(debugDrawId);
}

// Computes the best quadratic fit of input points between index
// `firstInputIndex` and `lastInputIndex` (included).
//
detail::BlendFitInfo computeBestFit(
    const SketchPointBuffer& input,
    Int i1,
    Int i2,
    const experimental::BlendFitSettings& settings,
    detail::FitBuffer& buffer) {

    detail::BlendFitInfo res;
    res.firstInputIndex = i1;
    res.lastInputIndex = i2;
    res.s1 = input[i1].s();
    res.s2 = input[i2].s();

    // Compute the best quadratic fit of the input points between
    // `firstInputIndex` and `lastInputIndex` (included).
    //
    res.bezier = quadraticFitWithFixedEndpoints(input, i1, i2, buffer);

    // Compute the max distance squared between the input points and the Bézier fit,
    // and check whether it is within the chosen threshold.
    //
    double distanceThreshold = settings.distanceThreshold;
    double distanceSquaredThreshold = distanceThreshold * distanceThreshold;
    auto [distanceSquared, index] = maxDistanceSquared(res.bezier, buffer);
    bool isWithinDistance = distanceSquared <= distanceSquaredThreshold;

    // Convert furthest index from index in `positions` to index in `input`.
    //
    res.furthestIndex = index + i1;

    // Determine whether we should use the flatness threshold.
    //
    Int numFitPoints = i2 - i1 + 1;
    double flatnessThreshold = settings.flatnessThreshold;
    bool enableFlatnessThreshold =
        (flatnessThreshold >= 0) && (numFitPoints > settings.flatnessThresholdMinPoints);

    // If enabled, compute the square of the flatness, and compare it with the
    // square of the flatness threshold. Alternatively, we could directly
    // define the flatness to be the square of the current flatness, but then
    // choosing a flatness threshold would be less intuitive since the scale
    // would be quadratic instead of linear.
    //
    //                             o        --> 2x taller means 2x less flat
    //         o
    //   o           o       o           o
    //
    //   B0 = (0, 0)         B0 = (0, 0)
    //   B1 = (2, 1)         B1 = (2, 2)
    //   B2 = (4, 0)         B2 = (4, 0)
    //   flatness  = 1       flatness  = 0.5
    //   flatness² = 1       flatness² = 0.25
    //
    bool isWithinFlatness = true;
    if (enableFlatnessThreshold) {
        double flatnessSquaredThreshold = flatnessThreshold * flatnessThreshold;
        double flatnessSquared = core::DoubleInfinity;
        flatnessThreshold *= flatnessThreshold;
        double der2 = res.bezier.secondDerivative().squaredLength();
        double l2 = (res.bezier.p2() - res.bezier.p0()).squaredLength();
        if (der2 > 0) {
            flatnessSquared = l2 / der2;
        }
        else {
            // Perfectly flat => flatnessSquared = core::DoubleInfinity;
        }
        isWithinFlatness = flatnessSquared >= flatnessSquaredThreshold;
    }

    res.isGoodFit = isWithinDistance && isWithinFlatness;
    return res;
}

void debugDraw(
    const geometry::Mat3d& transform,
    const geometry::QuadraticBezier2d& bezier) {

    using namespace graphics;
    using namespace geometry;

    GeometryViewPtr geometry;

    debugDraw(transform, [geometry, bezier](graphics::Engine* engine) mutable {
        if (!geometry) {
            // Create vertex data
            constexpr Int numSegments = 100;
            constexpr double du = 1.0 / numSegments;
            Vec2fArray vertData;
            for (Int i = 0; i <= numSegments; ++i) {
                double u = i * du;
                Vec2d derivative;
                Vec2d position = bezier.eval(u, derivative);
                Vec2f normal(derivative.normalized().orthogonalized());
                vertData.append(Vec2f(position));
                vertData.append(normal);
                vertData.append(Vec2f(position));
                vertData.append(-normal);
            }

            // Create instance data
            core::Color c = core::colors::red;
            float screenSpaceWidth = 2.0f;
            float hw = screenSpaceWidth * 0.5f;
            core::FloatArray instData = {0.f, 0.f, 1.f, hw, c.r(), c.g(), c.b(), c.a()};

            // Transfer to GPU
            auto layout = BuiltinGeometryLayout::XYDxDy_iXYRotWRGBA;
            geometry = engine->createTriangleStrip(layout);
            engine->updateVertexBufferData(geometry, std::move(vertData));
            engine->updateInstanceBufferData(geometry, std::move(instData));
        }
        // Draw
        engine->setProgram(graphics::BuiltinProgram::ScreenSpaceDisplacement);
        engine->draw(geometry);
    });
}

// This implements the "Dense" type of blend fit.
//
void computeDenseBlendFits(
    const SketchPointBuffer& input,
    const experimental::BlendFitSettings& settings_,
    core::Array<detail::BlendFitInfo>& fits_,
    detail::FitBuffer& buffer_) {

    // When growing the fit is not possible (or would be a bad fit), then we
    // can either shrink the fit, or move it by one point.
    //
    // Option 1 (shrink):
    //
    //   ------
    //    -----
    //
    // Option 2 (move):
    //
    //   ------
    //    ------
    //
    // If `preferMoveThanShrink` is true, then we prefer moving it (as long as
    // doing so results in a good fit). Otherwise, we always shrink.
    //
    constexpr bool preferMoveThanShrink = true;

    // Convenient aliases
    Int minFitPoints = settings_.minFitPoints;
    Int maxFitPoints = settings_.maxFitPoints;
    Int numStartFits = settings_.numStartFits;
    Int numInputPoints = input.length();

    // Compute first fit.
    //
    // We first compute the largest i2 in [i2Min, i2Max] such that [0, i2] is a
    // good fit and all [0, j] for j in [i2Min, i2] are good fits, then use the
    // range [i1, max(i2Min, i2 - numStartFits + 1)] as first fit.
    //
    // Note: the `if (numStartFits < i2Max - i2Min + 1)` condition is an
    // optimization: if false, we know that max(i2Min, i2 - numStartFits + 1)
    // is equal to i2Min for all i2 in [i2Min, i2Max], so there is no need to
    // compute such larger i2.
    //
    if (fits_.isEmpty()) {
        Int i1 = 0;
        Int i2Min = (std::min)(minFitPoints - 1, numInputPoints - 1);
        Int i2Max = (std::min)(maxFitPoints - 1, numInputPoints - 1);
        Int i2 = i2Min;
        if (numStartFits < i2Max - i2Min + 1) { // Note: false if numStartFits = IntMax
            for (i2 = i2Min; i2 <= i2Max; ++i2) {
                detail::BlendFitInfo fit =
                    computeBestFit(input, i1, i2, settings_, buffer_);
                if (!fit.isGoodFit) {
                    --i2;
                    break;
                }
            }
            i2 = (std::max)(i2Min, i2 - numStartFits + 1);
        }
        detail::BlendFitInfo fit = computeBestFit(input, i1, i2, settings_, buffer_);
        fits_.append(fit);
    }

    // Lambda to move the fit by one point:
    // Last fit: -----
    // New fit:   -----
    auto move = [&](Int i1, Int i2) {
        detail::BlendFitInfo fit =
            computeBestFit(input, i1 + 1, i2 + 1, settings_, buffer_);
        fits_.append(fit);
    };

    // Lambda to shink the fit by one point:
    // Last fit: -----
    // New fit:   ----
    auto shrink = [&](Int i1, Int i2) {
        detail::BlendFitInfo fit = computeBestFit(input, i1 + 1, i2, settings_, buffer_);
        fits_.append(fit);
    };

    // Lambda to move or shrink the fit by one point:
    // Last fit: -----
    // New fit:   ----- (if preferMoveThanShrink is true and it is a good fit)
    // New fit:   ----  (if preferMoveThanShrink is false or moving is a bad fit)
    auto moveOrShrink = [&](Int i1, Int i2) {
        if (preferMoveThanShrink) {
            detail::BlendFitInfo fitIfMoved =
                computeBestFit(input, i1 + 1, i2 + 1, settings_, buffer_);
            if (fitIfMoved.isGoodFit) {
                fits_.append(fitIfMoved);
            }
            else {
                shrink(i1, i2);
            }
        }
        else {
            shrink(i1, i2);
        }
    };

    // Lambda to test if there are still fits to be computed
    auto finished = [&]() {
        Int i2 = fits_.last().lastInputIndex;
        return i2 == numInputPoints - 1;
    };

    // Compute subsequent fits.
    //
    while (!finished()) {
        Int i1 = fits_.last().firstInputIndex;
        Int i2 = fits_.last().lastInputIndex;
        if (i2 - i1 + 1 == maxFitPoints) {
            if (i2 - i1 + 1 == minFitPoints) {
                // Fixed fit size => cannot grow or shrink
                move(i1, i2);
            }
            else {
                // Reached maxFitPoints => cannot grow
                moveOrShrink(i1, i2);
            }
        }
        else {
            // Growing is possible => grow by one point and test if good.
            // If good, keep it. Otherwise, either shrink or move by one point
            detail::BlendFitInfo fitIfGrown =
                computeBestFit(input, i1, i2 + 1, settings_, buffer_);
            if (fitIfGrown.isGoodFit) {
                // Growing is possible and good => grow
                fits_.append(fitIfGrown);
            }
            else if (i2 - i1 + 1 == minFitPoints) {
                // Growing is bad and cannot shrink => move
                move(i1, i2);
            }
            else {
                // Growing is bad and both shrink or move are possible
                moveOrShrink(i1, i2);
            }
        }
    }

    // Compute the last fits all sharing the last input point.
    //
    Int i1 = fits_.last().firstInputIndex;
    Int i2 = fits_.last().lastInputIndex;
    VGC_ASSERT(i2 == numInputPoints - 1);
    Int i1Min = i1 + 1;
    Int i1Max = i2 - minFitPoints + 1;
    if (numStartFits < i1Max - i1 + 1) { // Note: false if numStartFits = IntMax
        i1Max = i1 + numStartFits - 1;
    }
    for (i1 = i1Min; i1 <= i1Max; ++i1) { // Note: empty loop if i1Max < i1Min
        detail::BlendFitInfo fit = computeBestFit(input, i1, i2, settings_, buffer_);
        fits_.append(fit);
    }
}

// Determine minimum and maximum value of i2 based on Sparse Blend settings.
//
// In our experience, results are usually better when allowing
// consecutive fits to share their last input point, otherwise:
//
// 1. In would make it impossible in some cases to reach back `minFitPoints`
//    depending on `indexRatio`. For example, if `minFitPoints = 5` and
//    `indexRatio = 0.25`, then once we reach 8 fit points (i2 = i1 + 7),
//     then the next fit would have the following constraints:
//       i1_next = splitIndex(i1, i2, 0.25) = i1 + floor(7*0.25) = i1 + 1
//       i2_next >= i2 + 1 (since we would not allow i2_next = i2)
//     So: i2_next - i1_next >= i2 - i1 = 7
//     Which means that we can never go below 8 fit points anymore.
//     To make it possible to go back down to 5 fit points, one would have to
//     choose `indexRatio >= 2/5`, which may not be desirable. To go back down
//     to 3 fit points, one would have to choose `indexRatio >= 2/3`
//
// 2. More generally, it would make it impossible to decrease
//    numFitPoints fast enough from a large value to a small value, which
//    is necessary when transitionning from a low-curvature to a
//    high-curvature part of the curve, such as a sharp turn after a long
//    straight line. Therefore, the algorithm would be forced to include
//    in its output bad fits that go over the turn and cannot be well
//    approximated by a quadratic.
//
// However, note that typically we always have i1_next >= i1 + 1
// (see implementation of getSplitIndex()), which is necessary to
// ensure that the algorithm terminates. This makes the output somewhat
// asymmetrical, but it makes sense given the "forward-oriented" nature
// of the algorithm (running it with the input points reversed would
// give a different result anyway, even if we didn't allow fits to
// share their end point)
//
// An alternative way to solve problems 1. and 2. while not allowing
// fits to share their end point might be to implement a more complex
// SplitStrategy that behaves differently for small numbers and large
// numbers or fit points, or a more complex algorithm that actually
// searches for the best i1_next based on the quality of fits (like we
// do for i2_next), instead of just directly setting the value of
// i1_next based on a simple formula from i1 and i2.
//
// Finally, note that since i1 is determined via a simple formula
// (e.g., i1_next = i1 + floor((i2-i1)*0.25)), while i2 is determined
// as the largest possible index that still allows for a good fit, then
// this allows going instantly from a small number of fit points to a
// large number of fit points, but not the other way around.
//
// The above problem is one reason why results can often be better when using
// the Dense approach, which does not have this issue.
//
// Fundamentally, this issue highlights once again the fact that the algorithm
// is no symmetrical, and it might be worth exploring other methdods to make
// this more symmetrical, such as limiting `i2_next - i1_next` to not be more
// than some factor of `i2 - i1`, or determining i1 in a more complex way than
// just the simple formula. Such limits are already implemented in the simple
// case where FitSplitType is RelativeToStart, in which case we do not allow
// i2 to grow more than splitOffset + 1 (see i2MaxOffset)
//
// A completely different way to achieve symmetrical results would be to
// compute the local fits independently from each other, e.g., for each input
// point i, compute the largest k such that [i - k, i + k] is a good fit.
// However, this would require some special case handling when consecutive fits
// do no satisfy fit1.i1 <= fit2.i1 and fit1.i2 <= fit2.i2, which is currently
// an assumption of the computeBlendBetweenFits() function.
//
std::pair<Int, Int> getSparseBlendIndexRange(
    const SketchPointBuffer& input,
    const core::Array<detail::BlendFitInfo>& fits_,
    const experimental::BlendFitSettings& settings_,
    Int i1) {

    Int numInputPoints = input.length();

    using experimental::FitSplitStrategy;
    using experimental::FitSplitType;
    const FitSplitStrategy& splitStrategy = settings_.splitStrategy;
    bool isRelativeToStart = splitStrategy.type() == FitSplitType::RelativeToStart;
    Int splitOffset = settings_.splitStrategy.offset();

    constexpr bool allowSharedLastInputIndex = true;
    constexpr Int i2MinOffset = allowSharedLastInputIndex ? 0 : 1;
    std::optional<Int> i2MaxOffset = std::nullopt;
    if (isRelativeToStart) {
        i2MaxOffset = splitOffset + 1; // +1 allows growing
    }
    Int i2Min = i1 + settings_.minFitPoints - 1;
    if (!fits_.isEmpty() && i2Min < fits_.last().lastInputIndex + i2MinOffset) {
        i2Min = fits_.last().lastInputIndex + i2MinOffset;
    }
    Int i2Max = i1 + settings_.maxFitPoints - 1;
    if (i2Max > numInputPoints - 1) {
        i2Max = numInputPoints - 1;
    }
    if (i2MaxOffset) {
        // If there is a maximum offset between an i2 and the next i2
        if (fits_.isEmpty()) {
            // then we want the first output fit to have numFitPoints <= minFitPoints
            //
            // XXX: Do we really want that? For example, we later implemented
            // computeDenseBlendFits(), which has the ability for
            // the first fit to either start at minFitPoints (like here), or be
            // as large as possible, or be as large as possible while enforcing
            // that a given number of fits share the first input point (see
            // numStartFits). The latter strategy seems to improve the results
            // for the dense case, but it may also apply to the sparse case.
            //
            if (i2Max > settings_.minFitPoints - 1) {
                i2Max = settings_.minFitPoints - 1;
            }
        }
        else {
            // and subsequent fits to progressively grow from there.
            if (i2Max > fits_.last().lastInputIndex + *i2MaxOffset) {
                i2Max = fits_.last().lastInputIndex + *i2MaxOffset;
            }
        }
    }

    // Handle special case where we need to go lower than the min at the end
    // of the stroke, because there isn't enough input points. Example with
    // minFitPoints = maxFitPoints = 5 and splitStrategy = relativeToStart(2):
    //
    // Input points:  ........ (8)
    // Output fits:   ----- (5)
    //                  ----- (5)
    //                    ---- (4) <- cannot use 5, exceptionally
    //
    i2Min = (std::min)(i2Min, i2Max);

    return std::make_pair(i2Min, i2Max);
}

// The general idea of the algorithm is:
//
// 1. Initialize i1 to 0
//
// 2. Compute the largest i2 such that [i1..i2] (and all [i1..j] for
//    i1 < j < i2) can be well-approximated by a Bézier.
//
// 3. Add (i1, i2, bestFit([i1..i2])) to the list of output fits
//
// 4. Compute a splitIndex between i1 and i2 (see SplitStrategy)
//    and set i1 = splitIndex
//
// 5. Go to step 2 and repeat until i2 = numInputPoints - 1.
//
// In practice, further contraints can be set to fine-tune the behavior,
// e.g, force all output fits to have a fixed number of input point.
//
// The terminology "sparse" refers to the fact that the next i1 is computed via
// the SplitStrategy, therefore the output fits look like:
//
// Input points: .........
// Output fits:  ----
//                  ----
//                     ---
//
// Rather than:
//
// Input points: .........
// Output fits:  ----
//               -----
//               ------
//                -----
//                 ----
//                 -----
//                 ------
//                 -------
//                  ------
//                   -----
//                    ----
//
void computeSparseBlendFits(
    const SketchPointBuffer& input,
    const experimental::BlendFitSettings& settings_,
    core::Array<detail::BlendFitInfo>& fits_,
    detail::FitBuffer& buffer_) {

    // Convenient aliases
    using experimental::FitSplitStrategy;
    using experimental::FitSplitType;
    const FitSplitStrategy& splitStrategy = settings_.splitStrategy;
    bool isRelativeToStart = splitStrategy.type() == FitSplitType::RelativeToStart;
    Int splitOffset = settings_.splitStrategy.offset();
    Int numInputPoints = input.length();

    // Using relativeToStart(0) would cause the loop to not terminate. If one
    // wants to allow sharing first input indices between consecutive local
    // fits, it makes more sense to use the Dense mode.
    VGC_ASSERT(splitStrategy != FitSplitStrategy::relativeToStart(0));

    // Lambda to test if there are still fits to be computed
    auto finished = [&]() {
        if (fits_.isEmpty()) {
            // We need at least one fit, so we're not done if there is none
            return false;
        }
        Int i2 = fits_.last().lastInputIndex;
        if (i2 != numInputPoints - 1) {
            // We need the last fit to reach the last input point
            return false;
        }
        if (isRelativeToStart) {
            // Test if we should "keep shrinking" the last fit by adding
            // smaller fits that share the last input index.
            Int i1 = fits_.last().firstInputIndex;
            Int numFitPoints = i2 - i1 + 1;
            return numFitPoints <= settings_.minFitPoints + splitOffset - 1;
        }
        else {
            return true;
            // XXX: would it make sense to return false in some case with
            // indexRatio too? Or perhaps it would be cleaner to handle the
            // "isEmpty" and "keep shrinking" and  cases in different loops, as in
            // computeDenseBlendFits()?
        }
    };

    while (!finished()) {

        // Determine i1
        Int i1 = 0;
        if (!fits_.isEmpty()) {
            const detail::BlendFitInfo& lastFit = fits_.last();
            i1 = getSplitIndex(settings_.splitStrategy, lastFit);
        }

        // Determine i2 min/max range
        auto [i2Min, i2Max] = getSparseBlendIndexRange(input, fits_, settings_, i1);

        // Compute bestFit([i1..i2]) and increase i2 until a bad fit is found,
        // or i2 has reached its maxValue. Add the last good fit (or only fit,
        // if even the first fit was bad) to the output.
        //
        bool isFirstFitAttempt = true;
        detail::BlendFitInfo lastGoodFit;
        for (Int i2 = i2Min; i2 <= i2Max; ++i2) {
            detail::BlendFitInfo fit = computeBestFit(input, i1, i2, settings_, buffer_);
            if (fit.isGoodFit || isFirstFitAttempt) {
                lastGoodFit = fit;
                isFirstFitAttempt = false;
            }
            if (i2 == i2Max || !fit.isGoodFit) {
                fits_.append(lastGoodFit);
                break;
            }
        }
    }
}

void computeBlendFits(
    const SketchPointBuffer& input,
    const experimental::BlendFitSettings& settings,
    core::Array<detail::BlendFitInfo>& fits,
    Int& numStableFits,
    detail::FitBuffer& buffer) {

    // Remove all previously unstable fits.
    //
    Int oldNumStableFits = numStableFits;
    fits.resize(oldNumStableFits);

    // Compute new fits.
    //
    if (settings.type == experimental::BlendFitType::Dense) {
        computeDenseBlendFits(input, settings, fits, buffer);
    }
    else {
        computeSparseBlendFits(input, settings, fits, buffer);
    }

    // Determine the new number of stable fits. Note that the last fit is
    // always considered unstable, even if all the input points were stable.
    //
    // A fit can only be considered stable if its `lastInputIndex` is a stable
    // input point, and if the input point just after is also stable. Indeed,
    // if the input point just after is allowed to change, then maybe the fit
    // could grow to include this point, which means that the fit is unstable.
    //
    // Also, any fit sharing the same `firstInputIndex` as an unstable fit is
    // also considered unstable. This is important for the dense blend case,
    // where a given number of fits (`numStartFits`) can share their first/last
    // input point, making all of them unstable together.
    //
    // For example, with the following where fitting the whole input is good:
    //
    // [  0] ----- 0-4 (5)
    // [  1] ------ 0-5 (6)
    // [  2] ------- 0-6 (7)
    // [  3] -------- 0-7 (8)
    // [  4] --------- 0-8 (9)
    // [  5]  -------- 1-8 (8)
    // [  6]   ------- 2-8 (7)
    // [  7]    ------ 3-8 (6)
    // [  8]     ----- 4-8 (5)
    //
    // All of the fits should be considered unstable, since for example with
    // `numStartFits == 5`, the next iteration might be:
    //
    // [  0] ------ 0-5 (6)
    // [  1] ------- 0-6 (7)
    // [  2] -------- 0-7 (8)
    // [  3] --------- 0-8 (9)
    // [  4] ---------- 0-9 (10)
    // [  5]  --------- 1-9 (9)
    // [  6]   -------- 2-9 (8)
    // [  7]    ------- 3-9 (7)
    // [  8]     ------ 4-9 (6)
    //
    Int firstUnstableFit = fits.length() - 1;
    auto isUnstableInputPoint = [&](Int k) { //
        return k >= input.numStablePoints();
    };
    auto isUnstableFit = [&](Int j) {
        return isUnstableInputPoint(fits[j].lastInputIndex + 1);
    };
    auto startsLikePrevious = [&](Int j) {
        return fits[j - 1].firstInputIndex == fits[j].firstInputIndex;
    };
    while (firstUnstableFit > 0 && isUnstableFit(firstUnstableFit - 1)) {
        --firstUnstableFit;
    }
    while (firstUnstableFit > 0 && startsLikePrevious(firstUnstableFit)) {
        --firstUnstableFit;
    }
    Int newNumStableFits = std::max<Int>(0, firstUnstableFit);

    // Update cached value for numStableFits.

    numStableFits = newNumStableFits;
}

// Improve width by using Catmull-Rom interpolation instead linear by part.
// This prevents ugly C1-discontinuities of the width.
//
// For the first/last input point pair, there is no previous/next input
// point, so we duplicate the first/last point.
//
[[maybe_unused]] void improveWidths1( //
    const SketchPointBuffer& input,
    Int k,
    double u,
    SketchPoint& p) {

    const SketchPoint& p1 = input[k];
    const SketchPoint& p2 = input[k + 1];

    bool hasPrev = (k - 1) >= 0;
    bool hasNext = (k + 2) < input.length();

    double w1 = p1.width();
    double w2 = p2.width();
    double w0 = hasPrev ? input[k - 1].width() : w1;
    double w3 = hasNext ? input[k + 2].width() : w2;

    std::array<double, 4> widths = {w0, w1, w2, w3};
    geometry::CubicBezier1d bezier = geometry::uniformCatmullRomToBezier<double>(widths);

    p.setWidth(bezier.eval(u));
}

// Improve width by using Catmull-Rom interpolation instead linear by part.
// This prevents ugly C1-discontinuities of the width.
//
// For the first/last input point pair, there is no previous/next input
// point, so we invent a width via some basic extrapolation.
//
// Example for k = 0 with input.length() >= 3:
//
//            o w3 = input[2].width()
//           /
//          o w2   = input[1].width()
//         /
//        o w1     = input[0].width()
//       /
//      o w0       = w1 + (w1 - w2) = symmetry of w2 relative to w1
//
[[maybe_unused]] void improveWidths2( //
    const SketchPointBuffer& input,
    Int k,
    double u,
    SketchPoint& p) {

    const SketchPoint& p1 = input[k];
    const SketchPoint& p2 = input[k + 1];

    bool hasPrev = (k - 1) >= 0;
    bool hasNext = (k + 2) < input.length();

    double w1 = p1.width();
    double w2 = p2.width();
    double w0 = hasPrev ? input[k - 1].width() : w1 + (w1 - w2);
    double w3 = hasNext ? input[k + 2].width() : w2 + (w2 - w1);

    std::array<double, 4> widths = {w0, w1, w2, w3};
    geometry::CubicBezier1d bezier = geometry::uniformCatmullRomToBezier<double>(widths);

    p.setWidth(bezier.eval(u));
}

// Use the same extrapolated widths as improveWidths2 for the first/last input
// point, but in addition uses the distance between input points to provide
// better tangents.
//
[[maybe_unused]] void improveWidths3( //
    const SketchPointBuffer& input,
    Int k,
    double u,
    SketchPoint& p) {

    const SketchPoint& p1 = input[k];
    const SketchPoint& p2 = input[k + 1];

    bool hasPrev = (k - 1) >= 0;
    bool hasNext = (k + 2) < input.length();

    double w1 = p1.width();
    double w2 = p2.width();
    double w0 = hasPrev ? input[k - 1].width() : w1 + (w1 - w2);
    double w3 = hasNext ? input[k + 2].width() : w2 + (w2 - w1);

    // Distances between input points (chord-lengths)
    double d12 = p2.s() - p1.s();
    double d01 = hasPrev ? (p1.s() - input[k - 1].s()) : d12;
    double d23 = hasNext ? (input[k + 2].s() - p2.s()) : d12;
    double d012 = d01 + d12;
    double d123 = d12 + d23;

    // Fallback to uniform if we would otherwise have a division by zero
    if (!(d012 > 0 && d123 > 0)) {
        std::array<double, 4> widths = {w0, w1, w2, w3};
        geometry::CubicBezier1d bezier =
            geometry::uniformCatmullRomToBezier<double>(widths);
        p.setWidth(bezier.eval(u));
        return;
    }

    // Desired dw/ds at the start/end of the Bézier, using a simple heuristic
    double dw_ds_1 = (w2 - w0) / d012;
    double dw_ds_2 = (w3 - w1) / d123;

    // Compute an approximation of ds/du at the start/end of the Bézier blend
    // between p1 and p2.
    //
    // TODO: Can we do something more accurate by actually evaluating ds/du
    // just before/after the input points? Alternatively, could we enforce that
    // ds/du is linear between control points by evaluating the Bézier in
    // arc-length rather than in u-space? Also, maybe we could compute all
    // output widths after all output positions have already been computed, and
    // updateChordLenghts() was called, so that we have better length
    // information, instead of using the input chord lengths.
    //
    double ds_du_1 = d12;
    double ds_du_2 = d12;

    // Compute the knots of the width cubic Bézier satisfying our desired dw/ds
    // at the start/end of the Bézier.
    //
    // This uses the following property of a cubic Bézier:
    //
    //  dB/du(0) = 3 * (B1 - B0)
    //  dB/du(1) = 3 * (B3 - B2)
    //
    // Therefore we have:
    //
    //  B1 = B0 + 1/3 * dB/du(0) = B0 + 1/3 * dB/ds(0) * ds/du(0)
    //  B2 = B3 - 1/3 * dB/du(1) = B3 - 1/3 * dB/ds(1) * ds/du(1)
    //
    constexpr double oneThird = 1.0 / 3.0;
    double kw0 = w1;
    double kw1 = w1 + oneThird * dw_ds_1 * ds_du_1;
    double kw2 = w2 - oneThird * dw_ds_2 * ds_du_2;
    double kw3 = w2;

    geometry::CubicBezier1d widthBezier(kw0, kw1, kw2, kw3);
    p.setWidth(widthBezier.eval(u));
}

void computeBlendBetweenFits(
    const SketchPointBuffer& input,
    const experimental::BlendFitSettings& settings,
    const core::Array<detail::BlendFitInfo>& fits,
    Int numStableFits,
    SketchPointBuffer& output) {

    // Remove previously unstable points
    Int oldNumStableOutputPoints = output.numStablePoints();
    Int newNumStableOutputPoints = oldNumStableOutputPoints;
    output.resize(oldNumStableOutputPoints);

    // Add the first output point unless it was already stable
    if (oldNumStableOutputPoints == 0) {
        output.append(input.first());
        if (input.numStablePoints() > 0) {
            newNumStableOutputPoints = 1;
        }
    }

    double ds = settings.ds;
    double sMax = input.last().s();
    Int numInteriorSamples = core::ifloor<Int>(sMax / ds - 0.5);
    Int j1 = 0; // Index of first fit that contains s in its interior
    Int j2 = 0; // Index of last fit that contains s in its interior
    Int k = 0;  // Index of last pair of input points (k, k+1) s.t. input[k].s() < s
    Int jMax = fits.length() - 1;
    Int kMax = input.length() - 2;
    for (Int i = output.length(); i <= numInteriorSamples; ++i) {

        // Compute the s param corresponding to this output point
        double s = i * ds;

        // Compute the range of fits and pair of input points containing s
        while (j1 < jMax && fits[j1].s2 <= s) {
            ++j1;
        }
        while (j2 < jMax && fits[j2 + 1].s1 < s) {
            ++j2;
        }
        while (k < kMax && input[k + 1].s() < s) {
            ++k;
        }
        VGC_ASSERT(j1 <= j2);

        // Init output sketch point as linear interpolation of input point pair.
        // This provides initial values for pressure/width/timestamp/position.
        const SketchPoint& p1 = input[k];
        const SketchPoint& p2 = input[k + 1];
        double s1 = p1.s();
        double s2 = p2.s();
        double v = (s - s1) / (s2 - s1);
        SketchPoint p = core::fastLerp(p1, p2, v);

        // Improve width by using Catmull-Rom instead of linear interpolation
        improveWidths3(input, k, v, p);

        // Compute position as weighted average of local fits.
        //
        // For now we evaluate each fit at the Bézier u-param:
        //
        //   u = (s - fit.s1) / (fit.s2 - fit.s1)
        //
        // This is not great since Bézier u-param is not linear in
        // arclength, so we may end up averaging parts of fits that
        // are actually far from each others:
        //
        //        s:  0              5              10             15
        //    input:  +              +              +              +
        //  fits[0]:  .    .   .  . ... .  .   .    .
        //  fits[1]:                 .    .   .  . ... .  .   .    .
        //            ^              ^              ^              ^
        //        input[0].s     input[1].s     input[2].s     input[2].s
        //        fits[0].s1                    fits[0].s2
        //                       fits[1].s1                    fits[1].s2
        //
        // In the viz above, fits are sampled at u = 0, 0.1, ..., 0.9, 1.0,
        // with ||dp/du|| being greater at the ends of the Bézier rather than
        // the middle, which is typical of 2D quadratic Béziers:
        //
        //   y   x = y^2
        //         = 0 1  4    9      16       25
        //  -5                                 .
        //  -4                        .
        //  -3                 .
        //  -2            .
        //  -1         .
        //   0        .
        //   1         .
        //   2            .
        //   3                 .
        //   4                        .
        //   5                                 .
        //
        // So if we evaluate for example at s = 7, we actually average the two
        // following samples:
        //
        //        s:  0              5     7        10             15
        //    input:  +              +              +              +
        //  fits[0]:  .    .   .  . ... .  .   .    .
        //  fits[1]:                 .  | .   .  . ... .  .   .    .
        //                              |     |
        //                              |     |
        //                fits[0].eval(u0)   fits[1].eval(u1)
        //          with u0 = (7-0)/(10-0)   with u1 = (7-5)/(15-10)
        //                  = 0.7                    = 0.2
        //         (8th sample of fits[0])   (3rd sample of fits[1])
        //
        // We can see that it doesn't make so much sense to average these, and it
        // would have instead been better to average values like so:
        //
        //        s:  0              5     7        10             15
        //    input:  +              +              +              +
        //  fits[0]:  .    .   .  . ... .  .   .    .
        //  fits[1]:                 .    .|  .  . ... .  .   .    .
        //                                 |
        //
        //                fits[0].eval(u0)   fits[1].eval(u1)
        //                  with u0 ~= 0.8   with u1 ~= 0.125
        //         (9th sample of fits[0])   (1/4 between 2nd and 3rd sample of fits[1])
        //
        // Such better values for u could be computed via an approximation of
        // the inverse of s(u), but it isn't cheap.
        //
        // Finally, an orthogonal way to improve which u value to use could be
        // to take into account, for each input point k and each fit j
        // (overlapping k), what u-param does the input point k has respective
        // to the fit j (these values have already been computed in the
        // FitBuffer, but then discarded). These values indeed provide some
        // kind of correspondence mapping between the overlapping fits.
        //
        geometry::Vec2d position;
        double totalWeight = 0;
        for (Int j = j1; j <= j2; ++j) {
            const detail::BlendFitInfo& fit = fits[j];
            double u = (s - fit.s1) / (fit.s2 - fit.s1);
            double t = 2 * u - 1;
            double weight = (1 - t) * (1 - t) * (1 + t) * (1 + t);
            totalWeight += weight;
            position += weight * fit.bezier.eval(u);
        }
        position /= totalWeight;
        p.setPosition(position);

        // Append output point
        output.append(p);

        // Determine whether the output point is stable, that is,
        // only influenced by stable fits.
        if (j2 < numStableFits) {
            newNumStableOutputPoints = i + 1;
        }
    }

    // Add the last output point.
    // Note: the last output point is never stable.
    output.append(input.last());

    // Update chord-lengths and number of stable output points.
    output.updateChordLengths();
    output.setNumStablePoints(newNumStableOutputPoints);
}

void debugFits(
    const core::Array<detail::BlendFitInfo>& fits,
    const geometry::Mat3d& transform) {

    if (!enableDebugFits) {
        return;
    }

    std::string str = "\n";
    debugDrawClear();
    Int i = 0;
    for (const auto& fit : fits) {
        Int numFitPoints = fit.lastInputIndex - fit.firstInputIndex + 1;
        std::string whitespace(fit.firstInputIndex, ' ');
        std::string dashes(numFitPoints, '-');
        str += core::format( //
            "[{:>3}] {}{} {}-{} ({}){}\n",
            i,
            whitespace,
            dashes,
            fit.firstInputIndex,
            fit.lastInputIndex,
            numFitPoints,
            fit.isGoodFit ? "" : "*");
        ++i;
        debugDraw(transform, fit.bezier);
    }
    VGC_DEBUG(LogVgcToolsSketch, str);
}

} // namespace

QuadraticBlendPass::QuadraticBlendPass()
    : SketchPass()
    , settings_() {
}

QuadraticBlendPass::QuadraticBlendPass(const experimental::BlendFitSettings& settings)
    : SketchPass()
    , settings_(settings) {

    // Ensure settings have safe values.
    // XXX: sanitize instead of throw?
    VGC_ASSERT(settings_.minFitPoints > 1);
    VGC_ASSERT(settings_.maxFitPoints >= settings_.minFitPoints);
}

void QuadraticBlendPass::setSettings(const experimental::BlendFitSettings& settings) {
    checkCanChangeSettings(*this);
    settings_ = settings;
}

void QuadraticBlendPass::doReset() {
    fits_.clear();
    numStableFits_ = 0;
}

void QuadraticBlendPass::doUpdateFrom(
    const SketchPointBuffer& input,
    SketchPointBuffer& output) {

    Int numInputPoints = input.length();

    if (handleSmallInputWithFixedEndpoints(input, output)) {
        return;
    }
    VGC_ASSERT(numInputPoints > 2);

    // Compute overlapping Bézier fits from the input point
    //
    computeBlendFits(input, settings_, fits_, numStableFits_, buffer_);
    debugFits(fits_, transformMatrix());

    // Compute a uniform sampling of the blend between the Bézier fits.
    //
    computeBlendBetweenFits(input, settings_, fits_, numStableFits_, output);
}

} // namespace vgc::tools
