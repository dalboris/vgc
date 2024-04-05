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

#include <vgc/vacomplex/inbetweenvertex.h>

namespace vgc::vacomplex {

geometry::Rect2d InbetweenVertex::boundingBoxAt(core::AnimTime t) const {
    // TODO
    VGC_UNUSED(t);
    return geometry::Rect2d::empty;
}

geometry::Vec2d InbetweenVertex::position(core::AnimTime) const {
    // XXX todo interp
    return geometry::Vec2d();
}

void InbetweenVertex::substituteKeyVertex_(KeyVertex*, KeyVertex*) {
    // TODO
}

void InbetweenVertex::substituteKeyEdge_(const KeyHalfedge&, const KeyHalfedge&) {
    // no-op
}

void InbetweenVertex::debugPrint_(core::StringWriter& out) {
    // TODO: more debug info
    out << core::format( //
        "{:<12}",
        "InbetweenVertex");
}

} // namespace vgc::vacomplex
