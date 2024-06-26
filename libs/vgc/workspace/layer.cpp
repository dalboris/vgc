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

#include <vgc/workspace/layer.h>

#include <vgc/workspace/workspace.h>

namespace vgc::workspace {

std::optional<core::StringId> Layer::domTagName() const {
    return dom::strings::layer;
}

geometry::Rect2d Layer::boundingBox(core::AnimTime t) const {
    // todo, union of children
    VGC_UNUSED(t);
    return geometry::Rect2d::empty;
}

void Layer::onPaintDraw(graphics::Engine* engine, core::AnimTime t, PaintOptions flags)
    const {

    VGC_UNUSED(engine);
    VGC_UNUSED(t);
    VGC_UNUSED(flags);
}

ElementStatus Layer::updateFromDom_(Workspace* workspace) {
    //dom::Element* const domElement = this->domElement();

    VGC_UNUSED(workspace);

    vacomplex::Node* node = vacNode();
    vacomplex::Group* group =
        node ? node->toCellUnchecked()->toGroupUnchecked() : nullptr;
    if (!group) {
        VacElement* parentElement = parentVacElement();
        if (!parentElement) {
            return ElementStatus::ErrorInParent;
        }
        vacomplex::Node* parentNode = parentElement->vacNode();
        if (!parentNode) {
            return ElementStatus::ErrorInParent;
        }
        group = vacomplex::ops::createGroup(parentNode->toGroupUnchecked());
        setVacNode(group);
    }

    // todo: set attributes
    // ...

    return ElementStatus::Ok;
}

void Layer::updateFromVac_(vacomplex::NodeModificationFlags flags) {
    // TODO
    VGC_UNUSED(flags);
}

} // namespace vgc::workspace
