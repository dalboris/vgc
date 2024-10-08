// Copyright 2022 The VGC Developers
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

#ifndef VGC_VACOMPLEX_KEYEDGE_H
#define VGC_VACOMPLEX_KEYEDGE_H

#include <memory>
#include <utility>

#include <vgc/core/arithmetic.h>
#include <vgc/geometry/rect2.h>
#include <vgc/geometry/stroke.h>
#include <vgc/vacomplex/api.h>
#include <vgc/vacomplex/cell.h>
#include <vgc/vacomplex/keyedgedata.h>

namespace vgc::vacomplex {

class KeyVertex;
class KeyHalfedge;

class VGC_VACOMPLEX_API KeyEdge final : public SpatioTemporalCell<EdgeCell, KeyCell> {
private:
    friend detail::Operations;
    friend KeyEdgeData;

    explicit KeyEdge(core::Id id, core::AnimTime t) noexcept
        : SpatioTemporalCell(id, t)
        , data_(detail::KeyEdgePrivateKey{}, this) {
    }

public:
    VGC_VACOMPLEX_DEFINE_SPATIOTEMPORAL_CELL_CAST_METHODS(Key, Edge)

    virtual ~KeyEdge();

    KeyVertex* startVertex() const {
        return startVertex_;
    }

    KeyVertex* endVertex() const {
        return endVertex_;
    }

    KeyEdgeData& data() {
        return data_;
    }

    const KeyEdgeData& data() const {
        return data_;
    }

    bool snapGeometry();

    const geometry::AbstractStroke2d* stroke() const {
        return data_.stroke();
    }

    geometry::CurveSamplingQuality strokeSamplingQuality() const {
        return data_.samplingQuality();
    }

    std::shared_ptr<const geometry::StrokeSampling2d> strokeSamplingShared() const {
        return data_.strokeSamplingShared();
    }

    std::shared_ptr<const geometry::StrokeSampling2d>
    strokeSamplingShared(core::AnimTime t) const override {
        VGC_UNUSED(t);
        return data_.strokeSamplingShared();
    }

    const geometry::StrokeSampling2d& strokeSampling() const {
        return data_.strokeSampling();
    }

    const geometry::StrokeSample2dArray& strokeSamples() const {
        return data_.strokeSamples();
    }

    const geometry::Rect2d& centerlineBoundingBox() const {
        return data_.centerlineBoundingBox();
    }

    /// Computes and returns a new array of samples for this edge according to the
    /// given `parameters`.
    ///
    /// Unlike `strokeSampling()`, this function does not cache the result.
    ///
    geometry::StrokeSampling2d
    computeStrokeSampling(geometry::CurveSamplingQuality quality) const {
        return data_.computeStrokeSampling(quality);
    }

    bool isStartVertex(const VertexCell* v) const override;

    bool isEndVertex(const VertexCell* v) const override;

    bool isClosed() const override {
        return !startVertex_;
    }

    /// Returns the angle, in radians and in the interval (-π, π],
    /// between the X axis and the start tangent.
    ///
    double startAngle() const;

    /// Returns the angle, in radians and in the interval (-π, π],
    /// between the X axis and the reversed end tangent.
    ///
    double endOppositeAngle() const;

    /// Returns the contribution of this edge to the winding number at
    /// the given `position` in edge space.
    ///
    /// The `direction` is used to prevent the end vertex from contributing.
    /// It is used to not count endpoint contributions twice in cycles.
    ///
    Int computeWindingContributionAt(const geometry::Vec2d& point) const;

    geometry::Rect2d boundingBox() const override {
        return data_.centerlineBoundingBox();
    }

    geometry::Rect2d boundingBoxAt(core::AnimTime t) const override {
        if (existsAt(t)) {
            return boundingBox();
        }
        return geometry::Rect2d::empty;
    }

protected:
    bool updateGeometryFromBoundary() override;

private:
    KeyVertex* startVertex_ = nullptr;
    KeyVertex* endVertex_ = nullptr;

    // position and orientation when not bound to vertices?
    // detail::Transform2d transform_;

    KeyEdgeData data_;

    void substituteKeyVertex_(KeyVertex* oldVertex, KeyVertex* newVertex) override;

    void substituteKeyEdge_(
        const KeyHalfedge& oldHalfedge,
        const KeyHalfedge& newHalfedge) override;

    void debugPrint_(core::StringWriter& out) override;
};

} // namespace vgc::vacomplex

#endif // VGC_VACOMPLEX_KEYEDGE_H
