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

#ifndef VGC_VACOMPLEX_INBETWEENEDGE_H
#define VGC_VACOMPLEX_INBETWEENEDGE_H

#include <vgc/vacomplex/api.h>
#include <vgc/vacomplex/cell.h>

namespace vgc::vacomplex {

class VGC_VACOMPLEX_API InbetweenEdge final
    : public SpatioTemporalCell<EdgeCell, InbetweenCell> {

private:
    friend detail::Operations;

    explicit InbetweenEdge(core::Id id) noexcept
        : SpatioTemporalCell(id) {
    }

public:
    VGC_VACOMPLEX_DEFINE_SPATIOTEMPORAL_CELL_CAST_METHODS(Inbetween, Edge)

    bool isStartVertex(VertexCell* /*v*/) const override {
        // TODO: check whether v is one of the start vertices of this inbetween edge.
        return false;
    }

    bool isEndVertex(VertexCell* /*v*/) const override {
        // TODO: check whether v is one of the end vertices of this inbetween edge.
        return false;
    }

    bool isClosed() const override {
        // TODO: return whether this inbetween edge is open or closed.
        return false;
    }
};

} // namespace vgc::vacomplex

#endif // VGC_VACOMPLEX_INBETWEENEDGE_H