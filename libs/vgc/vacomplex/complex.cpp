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

#include <vgc/vacomplex/complex.h>

#include <vgc/vacomplex/logcategories.h>

#include <vgc/vacomplex/detail/operations.h>

namespace vgc::vacomplex {

Complex::Complex(CreateKey key)
    : Object(key) {

    resetRoot();
}

void Complex::onDestroyed() {
    clear();
}

ComplexPtr Complex::create() {
    return core::createObject<Complex>();
}

// TODO: Move to Operations
//
void Complex::clear() {

    isBeingCleared_ = true;

    // Remove all the nodes
    NodePtrMap nodesCopy = std::move(nodes_);
    nodes_ = NodePtrMap();

    // Add the removal of all the nodes to the diff
    ComplexDiff diff;
    for (const auto& node : nodesCopy) {
        diff.onNodeDestroyed_(node.first);
    }
    nodesChanged().emit(diff);

    isBeingCleared_ = false;
    root_ = nullptr;
    ++version_;
}

Group* Complex::resetRoot() {
    if (isBeingCleared_) {
        return nullptr;
    }
    clear(); // should be an operation
    detail::Operations ops(this);
    root_ = ops.createRootGroup();
    return root_;
}

Node* Complex::find(core::Id id) const {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? it->second.get() : nullptr;
}

Cell* Complex::findCell(core::Id id) const {
    Node* n = find(id);
    return (n && n->isCell()) ? static_cast<Cell*>(n) : nullptr;
}

Group* Complex::findGroup(core::Id id) const {
    Node* n = find(id);
    return (n && n->isGroup()) ? static_cast<Group*>(n) : nullptr;
}

bool Complex::containsNode(core::Id id) const {
    return find(id) != nullptr;
}

Complex::VertexRange Complex::vertices() const {
    core::Array<VertexCell*> result;
    for (const auto& it : nodes_) {
        Node* node = it.second.get();
        if (Cell* cell = node->toCell()) {
            if (VertexCell* vc = cell->toVertexCell()) {
                result.append(vc);
            }
        }
    }
    return result;
}

Complex::EdgeRange Complex::edges() const {
    core::Array<EdgeCell*> result;
    for (const auto& it : nodes_) {
        Node* node = it.second.get();
        if (Cell* cell = node->toCell()) {
            if (EdgeCell* ec = cell->toEdgeCell()) {
                result.append(ec);
            }
        }
    }
    return result;
}

Complex::FaceRange Complex::faces() const {
    core::Array<FaceCell*> result;
    for (const auto& it : nodes_) {
        Node* node = it.second.get();
        if (Cell* cell = node->toCell()) {
            if (FaceCell* fc = cell->toFaceCell()) {
                result.append(fc);
            }
        }
    }
    return result;
}

Complex::VertexRange Complex::vertices(core::AnimTime t) const {
    core::Array<VertexCell*> result;
    for (const auto& it : nodes_) {
        Node* node = it.second.get();
        if (Cell* cell = node->toCell()) {
            if (VertexCell* vc = cell->toVertexCell()) {
                if (vc->existsAt(t)) {
                    result.append(vc);
                }
            }
        }
    }
    return result;
}

Complex::EdgeRange Complex::edges(core::AnimTime t) const {
    core::Array<EdgeCell*> result;
    for (const auto& it : nodes_) {
        Node* node = it.second.get();
        if (Cell* cell = node->toCell()) {
            if (EdgeCell* ec = cell->toEdgeCell()) {
                if (ec->existsAt(t)) {
                    result.append(ec);
                }
            }
        }
    }
    return result;
}

Complex::FaceRange Complex::faces(core::AnimTime t) const {
    core::Array<FaceCell*> result;
    for (const auto& it : nodes_) {
        Node* node = it.second.get();
        if (Cell* cell = node->toCell()) {
            if (FaceCell* fc = cell->toFaceCell()) {
                if (fc->existsAt(t)) {
                    result.append(fc);
                }
            }
        }
    }
    return result;
}

void Complex::setSamplingQuality(geometry::CurveSamplingQuality quality) {
    if (quality != samplingQuality_) {
        samplingQuality_ = quality;
        for (const auto& it : nodes_) {
            Node* node = it.second.get();
            if (Cell* cell = node->toCell()) {
                if (KeyEdge* ke = cell->toKeyEdge()) {
                    detail::DefaultSamplingQualitySetter::set(ke->data(), quality);
                }
            }
        }
    }
    // TODO: emit something?
}

void Complex::setSnapSettings(const geometry::CurveSnapSettings& settings) {
    snapSettings_ = settings;
}

namespace {

void debugPrintRec(core::StringWriter& out, Node* node, Int indent) {
    out << core::format("{:<{}}", "", indent);
    node->debugPrint(out);
    out << "\n";
    if (Group* group = node->toGroup()) {
        ++indent;
        Node* child = group->firstChild();
        while (child) {
            debugPrintRec(out, child, indent);
            child = child->nextSibling();
        }
    }
}

} // namespace

void Complex::debugPrint() {
    std::string out_;
    core::StringWriter out(out_);
    out << core::format("{}\n", ptr(this));
    if (root_) {
        debugPrintRec(out, root_, 0);
    }
    VGC_DEBUG(LogVgcVacomplex, out_);
}

Node* topMostInGroup(Group* group, core::ConstSpan<Node*> nodes) {
    Node* node = group->lastChild();
    while (node) {
        if (nodes.contains(node)) {
            return node;
        }
        node = node->previousSibling();
    }
    return nullptr;
}

Node* bottomMostInGroup(Group* group, core::ConstSpan<Node*> nodes) {
    Node* node = group->firstChild();
    while (node) {
        if (nodes.contains(node)) {
            return node;
        }
        node = node->nextSibling();
    }
    return nullptr;
}

Node* topMostInGroupAbove(Node* node_, core::ConstSpan<Node*> nodes) {
    Node* node = node_->parentGroup()->lastChild();
    while (node) {
        if (node == node_) {
            return nullptr;
        }
        if (nodes.contains(node)) {
            return node;
        }
        node = node->previousSibling();
    }
    return nullptr;
}

Node* bottomMostInGroupBelow(Node* node_, core::ConstSpan<Node*> nodes) {
    Node* node = node_->parentGroup()->firstChild();
    while (node) {
        if (node == node_) {
            return nullptr;
        }
        if (nodes.contains(node)) {
            return node;
        }
        node = node->nextSibling();
    }
    return nullptr;
}

} // namespace vgc::vacomplex
