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

#include <vgc/workspace/workspace.h>

#include <functional>
#include <memory>

#include <vgc/core/boolguard.h>
#include <vgc/dom/strings.h>
#include <vgc/vacomplex/algorithms.h> // boundary, star, closure, opening
#include <vgc/vacomplex/complex.h>
#include <vgc/vacomplex/operations.h>
#include <vgc/workspace/edge.h>
#include <vgc/workspace/face.h>
#include <vgc/workspace/layer.h>
#include <vgc/workspace/logcategories.h>
#include <vgc/workspace/vertex.h>

namespace vgc::workspace {

namespace detail {

struct VacElementLists {
    // groups are in Dfs order
    core::Array<Element*> groups;
    core::Array<Element*> keyVertices;
    core::Array<Element*> keyEdges;
    core::Array<Element*> keyFaces;
    core::Array<Element*> inbetweenVertices;
    core::Array<Element*> inbetweenEdges;
    core::Array<Element*> inbetweenFaces;
};

ScopedUndoGroup createScopedUndoGroup(Workspace* workspace, core::StringId name) {
    core::UndoGroup* undoGroup = nullptr;
    core::History* history = workspace->history();
    if (history) {
        undoGroup = history->createUndoGroup(name);
    }
    return detail::ScopedUndoGroup(undoGroup);
}

} // namespace detail

namespace {

// Assumes (parent == it->parent()).
template<
    typename Node,
    typename TreeLinksGetter = vacomplex::detail::TreeLinksGetter<Node>>
void iterDfsPreOrderSkipChildren(Node*& it, Int& depth, core::TypeIdentity<Node>* root) {
    Node* next = nullptr;
    // breadth next
    while (it) {
        next = TreeLinksGetter::nextSibling(it);
        if (next) {
            it = next;
            return;
        }
        // go up
        Node* p = TreeLinksGetter::parent(it);
        it = p;
        --depth;
        if (it == root) {
            it = nullptr;
            return;
        }
        if (p) {
            p = TreeLinksGetter::parent(p);
        }
    }
}

// Assumes (parent == it->parent()).
template<
    typename Node,
    typename TreeLinksGetter = vacomplex::detail::TreeLinksGetter<Node>>
void iterDfsPreOrder(Node*& it, Int& depth, core::TypeIdentity<Node>* root) {
    Node* next = nullptr;
    // depth first
    next = TreeLinksGetter::firstChild(it);
    if (next) {
        ++depth;
        it = next;
        return;
    }
    // breadth next
    iterDfsPreOrderSkipChildren(it, depth, root);
}

template<
    typename Node,
    typename TreeLinksGetter = vacomplex::detail::TreeLinksGetter<Node>>
void iterDfsPreOrder(
    Node*& it,
    Int& depth,
    core::TypeIdentity<Node>* root,
    bool skipChildren) {

    if (skipChildren) {
        iterDfsPreOrderSkipChildren(it, depth, root);
    }
    else {
        iterDfsPreOrder(it, depth, root);
    }
}

template<
    typename Node,
    typename TreeLinksGetter = vacomplex::detail::TreeLinksGetter<Node>>
void visitDfsPreOrder(Node* root, std::function<void(core::TypeIdentity<Node>*, Int)> f) {
    Node* e = root;
    Int depth = 0;
    while (e) {
        f(e, depth);
        iterDfsPreOrder(e, depth, root);
    }
}

template<
    typename Node,
    typename TreeLinksGetter = vacomplex::detail::TreeLinksGetter<Node>>
void visitDfs(
    Node* root,
    const std::function<bool(core::TypeIdentity<Node>*, Int)>& preOrderFn,
    const std::function<void(core::TypeIdentity<Node>*, Int)>& postOrderFn) {

    Int depth = 0;
    Node* node = root;
    while (node) {
        if (preOrderFn(node, depth)) {
            // depth first, go down
            Node* firstChild = TreeLinksGetter::firstChild(node);
            if (firstChild) {
                ++depth;
                node = firstChild;
                continue;
            }
        }
        postOrderFn(node, depth); // post-order leaf
        // breadth next
        Node* next = nullptr;
        while (node) {
            next = TreeLinksGetter::nextSibling(node);
            if (next) {
                node = next;
                break;
            }
            // go up
            Node* parent = TreeLinksGetter::parent(node);
            if (parent == root) {
                node = nullptr;
                depth = 0;
                break;
            }
            --depth;
            node = parent;
            if (node) {
                postOrderFn(node, depth); // post-order parent
                parent = TreeLinksGetter::parent(node);
            }
        }
    }
}

} // namespace

Workspace::Workspace(CreateKey key, dom::DocumentPtr document)
    : Object(key)
    , document_(document) {

    document->changed().connect(onDocumentDiff());

    vac_ = vacomplex::Complex::create();
    if (auto vac = vac_.lock()) {
        vac->cellSamplingQualityChanged().connect(onVacCellSamplingQualityChanged_Slot());
        vac->nodesChanged().connect(onVacNodesChanged());
    }

    rebuildFromDom();
}

void Workspace::onDestroyed() {

    elementByVacInternalId_.clear();
    for (const auto& p : elements_) {
        Element* e = p.second.get();
        VacElement* ve = e->toVacElement();
        if (ve) {
            // the whole VAC is cleared afterwards
            ve->vacNode_ = nullptr;
        }
        while (!e->dependents_.isEmpty()) {
            Element* dependent = e->dependents_.pop();
            dependent->dependencies_.removeOne(e);
            dependent->onDependencyRemoved_(e);
            e->onDependentElementRemoved_(dependent);
        }
    }
    elements_.clear();
    rootVacElement_ = nullptr;
    elementsWithError_.clear();
    elementsToUpdateFromDom_.clear();
    vac_ = nullptr;
    document_ = nullptr;

    SuperClass::onDestroyed();
}

namespace {

std::once_flag initOnceFlag;

template<typename T>
std::unique_ptr<Element> makeUniqueElement(Workspace* workspace) {
    return std::make_unique<T>(workspace);
}

} // namespace

WorkspacePtr Workspace::create(dom::DocumentPtr document) {

    namespace ds = dom::strings;

    std::call_once(initOnceFlag, []() {
        registerElementClass_(ds::vgc, &makeUniqueElement<Layer>);
        registerElementClass_(ds::layer, &makeUniqueElement<Layer>);
        registerElementClass_(ds::vertex, &makeUniqueElement<VacKeyVertex>);
        registerElementClass_(ds::edge, &makeUniqueElement<VacKeyEdge>);
        registerElementClass_(ds::face, &makeUniqueElement<VacKeyFace>);
    });

    return core::createObject<Workspace>(document);
}

void Workspace::registerElementClass_(
    core::StringId tagName,
    ElementCreator elementCreator) {

    elementCreators_()[tagName] = elementCreator;
}

void Workspace::sync() {
    if (auto document = document_.lock()) {
        document->emitPendingDiff();
    }
}

void Workspace::rebuildFromDom() {
    if (auto document = document_.lock()) {
        rebuildWorkspaceTreeFromDom_();
        rebuildVacFromWorkspaceTree_();
        lastSyncedDomVersionId_ = document->versionId();
        changed().emit();
    }
}

bool Workspace::updateElementFromDom(Element* element) {
    if (element->isBeingUpdated_) {
        VGC_ERROR(LogVgcWorkspace, "Cyclic update dependency detected.");
        return false;
    }
    if (element->hasPendingUpdateFromDom_) {
        element->isBeingUpdated_ = true;
        const ElementStatus oldStatus = element->status_;
        const ElementStatus newStatus = element->updateFromDom_(this);

        if (!newStatus) {
            if (oldStatus == ElementStatus::Ok) {
                elementsWithError_.emplaceLast(element);
            }
        }
        else if (!oldStatus) {
            elementsWithError_.removeOne(element);
        }

        element->status_ = newStatus;
        element->isBeingUpdated_ = false;
        clearPendingUpdateFromDom_(element);
    }
    return true;
}

std::optional<Element*> Workspace::getElementFromPathAttribute(
    const dom::Element* domElement,
    core::StringId attrName,
    core::StringId tagNameFilter) const {

    std::optional<dom::Element*> domTargetElement =
        domElement->getElementFromPathAttribute(attrName, tagNameFilter);

    if (!domTargetElement.has_value()) {
        return std::nullopt;
    }

    dom::Element* domTargetElementValue = domTargetElement.value();
    if (!domTargetElementValue) {
        return nullptr;
    }

    auto it = elements_.find(domTargetElementValue->internalId());
    if (it == elements_.end()) {
        return nullptr;
    }

    return {it->second.get()};
}

void Workspace::visitDepthFirstPreOrder(
    const std::function<void(Element*, Int)>& preOrderFn) {
    workspace::visitDfsPreOrder<Element>(vgcElement(), preOrderFn);
}

void Workspace::visitDepthFirst(
    const std::function<bool(Element*, Int)>& preOrderFn,
    const std::function<void(Element*, Int)>& postOrderFn) {
    workspace::visitDfs<Element>(vgcElement(), preOrderFn, postOrderFn);
}

namespace {

template<typename Fn, typename... Args>
void doReorder_(
    Workspace* workspace,
    core::StringId opName,
    Fn fn,
    core::ConstSpan<core::Id> elementIds,
    Args&&... args) {

    std::map<Layer*, core::Array<Element*>> byLayer;
    for (core::Id id : elementIds) {
        Element* item = workspace->find(id);
        if (!item) {
            continue;
        }
        Element* parent = item->parent();
        Layer* layer = dynamic_cast<Layer*>(parent);
        core::Array<Element*>& layerItems = byLayer[layer];
        if (!layerItems.contains(item)) {
            layerItems.append(item);
        }
    }

    auto undoGroup = detail::createScopedUndoGroup(workspace, opName);

    for (const auto& it : byLayer) {
        const core::Array<Element*>& layerItems = it.second;
        core::Array<vacomplex::Node*> layerNodes;
        for (Element* item : layerItems) {
            if (VacElement* vacItem = item->toVacElement()) {
                vacomplex::Node* node = vacItem->vacNode();
                if (node && !layerNodes.contains(node)) {
                    layerNodes.append(node);
                }
            }
        }
        fn(layerNodes, args...);
    }

    workspace->sync();
}

} // namespace

void Workspace::bringForward(core::ConstSpan<core::Id> elementIds, core::AnimTime t) {
    static core::StringId opName("workspace.bringForward");
    doReorder_(this, opName, &vacomplex::ops::bringForward, elementIds, t);
}

void Workspace::sendBackward(core::ConstSpan<core::Id> elementIds, core::AnimTime t) {
    static core::StringId opName("workspace.sendBackward");
    doReorder_(this, opName, &vacomplex::ops::sendBackward, elementIds, t);
}

void Workspace::bringToFront(core::ConstSpan<core::Id> elementIds, core::AnimTime t) {
    static core::StringId opName("workspace.bringToFront");
    doReorder_(this, opName, &vacomplex::ops::bringToFront, elementIds, t);
}

void Workspace::sendToBack(core::ConstSpan<core::Id> elementIds, core::AnimTime t) {
    static core::StringId opName("workspace.sendToBack");
    doReorder_(this, opName, &vacomplex::ops::sendToBack, elementIds, t);
}

core::Id Workspace::glue(core::ConstSpan<core::Id> elementIds) {
    core::Id resultId = -1;

    // Open history group
    static core::StringId opName("workspace.glue");
    detail::ScopedUndoGroup undoGroup = createScopedUndoGroup_(opName);

    core::Array<vacomplex::KeyVertex*> kvs;
    core::Array<vacomplex::KeyEdge*> openKes;
    core::Array<vacomplex::KeyEdge*> closedKes;
    bool hasOtherCells = false;
    bool hasNonVacElements = false;
    for (core::Id id : elementIds) {
        workspace::Element* element = find(id);
        if (!element) {
            continue;
        }
        vacomplex::Node* node = element->vacNode();
        if (!node || !node->isCell()) {
            hasNonVacElements = true;
            continue;
        }
        vacomplex::Cell* cell = node->toCellUnchecked();
        switch (cell->cellType()) {
        case vacomplex::CellType::KeyVertex: {
            kvs.append(cell->toKeyVertexUnchecked());
            break;
        }
        case vacomplex::CellType::KeyEdge: {
            vacomplex::KeyEdge* ke = cell->toKeyEdgeUnchecked();
            if (ke->isClosed()) {
                closedKes.append(ke);
            }
            else {
                openKes.append(ke);
            }
            break;
        }
        default:
            hasOtherCells = true;
            break;
        }
    }

    vacomplex::Cell* result = nullptr;

    if (hasOtherCells || hasNonVacElements) {
        // do not glue
    }
    else if (!kvs.isEmpty()) {
        if (openKes.isEmpty() && closedKes.isEmpty()) {
            geometry::Vec2d cog = {};
            for (vacomplex::KeyVertex* kv : kvs) {
                cog += kv->position();
            }
            cog /= core::narrow_cast<double>(kvs.length());
            result = vacomplex::ops::glueKeyVertices(kvs, cog);
        }
    }
    else if (!openKes.isEmpty()) {
        if (closedKes.isEmpty()) {
            result = vacomplex::ops::glueKeyOpenEdges(openKes);
        }
    }
    else if (!closedKes.isEmpty()) {
        result = vacomplex::ops::glueKeyClosedEdges(closedKes);
    }

    sync();

    if (result) {
        Element* e = this->findVacElement(result);
        if (e) {
            resultId = e->id();
        }
    }

    return resultId;
}

core::Array<core::Id> Workspace::unglue(core::ConstSpan<core::Id> elementIds) {
    core::Array<core::Id> result;

    core::Array<vacomplex::KeyVertex*> kvs;
    core::Array<vacomplex::KeyEdge*> kes;
    for (core::Id id : elementIds) {
        workspace::Element* element = find(id);
        if (!element) {
            continue;
        }
        vacomplex::Node* node = element->vacNode();
        if (!node || !node->isCell()) {
            continue;
        }
        vacomplex::Cell* cell = node->toCellUnchecked();
        switch (cell->cellType()) {
        case vacomplex::CellType::KeyVertex: {
            kvs.append(cell->toKeyVertexUnchecked());
            break;
        }
        case vacomplex::CellType::KeyEdge: {
            vacomplex::KeyEdge* ke = cell->toKeyEdgeUnchecked();
            kes.append(ke);
            break;
        }
        default:
            break;
        }
    }

    if (kvs.isEmpty() && kes.isEmpty()) {
        return result;
    }

    // Open history group
    static core::StringId opName("workspace.unglue");
    detail::ScopedUndoGroup undoGroup = createScopedUndoGroup_(opName);

    for (vacomplex::KeyEdge* targetKe : kes) {

        core::Array<vacomplex::KeyEdge*> ungluedKeyEdges =
            vacomplex::ops::unglueKeyEdges(targetKe);

        for (vacomplex::KeyEdge* ke : ungluedKeyEdges) {
            Element* e = this->findVacElement(ke);
            if (e) {
                result.append(e->id());
            }
        }
    }

    for (vacomplex::KeyVertex* targetKv : kvs) {

        core::Array<std::pair<core::Id, core::Array<vacomplex::KeyEdge*>>>
            ungluedKeyEdges;
        core::Array<vacomplex::KeyVertex*> ungluedKeyVertices =
            vacomplex::ops::unglueKeyVertices(targetKv, ungluedKeyEdges);

        for (vacomplex::KeyVertex* kv : ungluedKeyVertices) {
            Element* e = this->findVacElement(kv);
            if (e) {
                result.append(e->id());
            }
        }
    }

    sync();

    return result;
}

bool Workspace::cutGlueFace(core::ConstSpan<core::Id> elementIds) {

    vacomplex::KeyFace* targetKf = nullptr;
    vacomplex::KeyEdge* targetKe = nullptr;

    for (core::Id id : elementIds) {
        workspace::Element* element = find(id);
        if (!element) {
            continue;
        }
        vacomplex::Node* node = element->vacNode();
        if (!node || !node->isCell()) {
            continue;
        }
        vacomplex::Cell* cell = node->toCellUnchecked();
        switch (cell->cellType()) {
        case vacomplex::CellType::KeyFace: {
            vacomplex::KeyFace* kf = cell->toKeyFaceUnchecked();
            if (targetKf) {
                return false;
            }
            targetKf = kf;
            break;
        }
        case vacomplex::CellType::KeyEdge: {
            vacomplex::KeyEdge* ke = cell->toKeyEdgeUnchecked();
            if (targetKe) {
                return false;
            }
            targetKe = ke;
            break;
        }
        default:
            return false;
        }
    }

    if (!targetKf || !targetKe) {
        return false;
    }

    // Open history group
    static core::StringId opName("workspace.cutGlueFace");
    detail::ScopedUndoGroup undoGroup = createScopedUndoGroup_(opName);

    auto result = vacomplex::ops::cutGlueFace(targetKf, targetKe);
    bool success = result.edge() != nullptr;

    sync();

    return success;
}

core::Array<core::Id>
Workspace::simplify(core::ConstSpan<core::Id> elementIds, bool smoothJoins) {

    core::Array<core::Id> result;

    core::Array<vacomplex::KeyVertex*> kvs;
    core::Array<vacomplex::KeyEdge*> kes;
    for (core::Id id : elementIds) {
        workspace::Element* element = find(id);
        if (!element) {
            continue;
        }
        vacomplex::Node* node = element->vacNode();
        if (!node || !node->isCell()) {
            continue;
        }
        vacomplex::Cell* cell = node->toCellUnchecked();
        switch (cell->cellType()) {
        case vacomplex::CellType::KeyVertex: {
            kvs.append(cell->toKeyVertexUnchecked());
            break;
        }
        case vacomplex::CellType::KeyEdge: {
            kes.append(cell->toKeyEdgeUnchecked());
            break;
        }
        default:
            break;
        }
    }

    if (kvs.isEmpty() && kes.isEmpty()) {
        return result;
    }

    // Open history group
    static core::StringId opName("workspace.simplify");
    detail::ScopedUndoGroup undoGroup = createScopedUndoGroup_(opName);

    auto appendToResult = [&](vacomplex::Node* node) {
        Element* e = this->findVacElement(node);
        if (e) {
            result.append(e->id());
        }
    };

    for (vacomplex::KeyEdge* ke : kes) {
        vacomplex::Cell* cell = vacomplex::ops::uncutAtKeyEdge(ke);
        if (cell) {
            appendToResult(cell);
        }
        else {
            // uncut failed, return the edge id
            appendToResult(ke);
        }
    }

    for (vacomplex::KeyVertex* kv : kvs) {
        vacomplex::Cell* cell = vacomplex::ops::uncutAtKeyVertex(kv, smoothJoins);
        if (cell) {
            appendToResult(cell);
        }
        else {
            // uncut failed, return the vertex id
            appendToResult(kv);
        }
    }

    // TODO: filter out resulting ids that are no longer alive (lost in concatenation)

    sync();

    return result;
}

core::Array<core::Id>
Workspace::intersectWithGroup(core::ConstSpan<core::Id> elementIds) {

    core::Array<core::Id> result;

    // Add all edges from elements to `kes`, and the rest to `result`
    core::Array<vacomplex::KeyEdge*> kes;
    for (core::Id id : elementIds) {
        workspace::Element* element = find(id);
        if (!element) {
            result.append(id);
            continue;
        }
        vacomplex::Node* node = element->vacNode();
        if (!node || !node->isCell()) {
            result.append(id);
            continue;
        }
        vacomplex::Cell* cell = node->toCellUnchecked();
        switch (cell->cellType()) {
        case vacomplex::CellType::KeyEdge: {
            kes.append(cell->toKeyEdgeUnchecked());
            break;
        }
        default:
            result.append(id);
            break;
        }
    }

    if (kes.isEmpty()) {
        return result;
    }

    // Open history group
    static core::StringId opName("workspace.intersectWithGroup");
    detail::ScopedUndoGroup undoGroup = createScopedUndoGroup_(opName);

    // Perform the intersection
    vacomplex::IntersectResult r = vacomplex::ops::intersectWithGroup(kes);

    // Add all output cells of the intersection to the result
    for (vacomplex::Cell* c : r.outputKeyVertices()) {
        Element* e = this->findVacElement(c);
        if (e) {
            result.append(e->id());
        }
    }
    for (vacomplex::Cell* c : r.outputKeyEdges()) {
        Element* e = this->findVacElement(c);
        if (e) {
            result.append(e->id());
        }
    }

    sync();

    return result;
}

dom::DocumentPtr Workspace::cut(core::ConstSpan<core::Id> elementIds) {
    dom::DocumentPtr res = copy(elementIds);
    softDelete(elementIds);
    return res;
}

dom::DocumentPtr Workspace::copy(core::ConstSpan<core::Id> elementIds) {

    core::Array<Element*> elements;
    for (core::Id id : elementIds) {
        Element* element = find(id);
        if (!element || elements.contains(element)) {
            continue;
        }
        elements.append(element);
    }

    // get full dependency chain
    core::Array<Element*> elementsToCopy;
    while (!elements.isEmpty()) {
        Element* element = elements.pop();
        elementsToCopy.append(element);
        for (Element* dependency : element->dependencies()) {
            if (!elements.contains(dependency) && !elementsToCopy.contains(dependency)) {
                elements.append(dependency);
            }
        }
    }

    core::Array<dom::Node*> domNodesToCopy;
    domNodesToCopy.reserve(elementsToCopy.length());
    for (Element* e : elementsToCopy) {
        dom::Element* domElement = e->domElement();
        // TODO: what to do if domElement is null?
        domNodesToCopy.append(domElement);
    }

    return dom::Document::copy(domNodesToCopy);
}

core::Array<core::Id> Workspace::paste(dom::DocumentPtr pastedDoc) {

    auto document = document_.lock();
    if (!document) {
        return {};
    }

    // Delegate the pasting operation to the DOM.
    //
    // TODO: use active group element as parent.
    //
    dom::Node* parent = document->rootElement();
    core::Array<dom::Node*> nodes = document->paste(pastedDoc, parent);

    // Convert the Node* to Workspace IDs.
    //
    // Note: DOM IDs and Workspace IDs are the same, so we can
    // do this before the update from DOM.
    //
    core::Array<core::Id> res;
    res.reserve(nodes.length());
    for (dom::Node* node : nodes) {
        if (node->nodeType() == dom::NodeType::Element) {
            res.append(static_cast<dom::Element*>(node)->internalId());
        }
    }

    // Update from DOM.
    //
    sync();

    return res;
}

namespace {

// Note: VAC elements have soft and hard delete, non-VAC elements may have the
// same, so it would be best to have the delete method virtual in
// workspace::Element.
//
// Later, the default for VAC cells should probably be soft delete.
//
void hardDeleteElement(workspace::Element* element) {
    vacomplex::Node* node = element->vacNode();
    bool deleteIsolatedVertices = true;
    vacomplex::ops::hardDelete(node, deleteIsolatedVertices);
}

} // namespace

void Workspace::hardDelete(core::ConstSpan<core::Id> elementIds) {
    // Iterate over all elements to delete.
    //
    // For now, deletion is done via the DOM, so we need to sync() before
    // finding the next selected ID to check whether it still exists.
    //
    for (core::Id id : elementIds) {
        workspace::Element* element = find(id);
        if (element) {
            hardDeleteElement(element);
        }
    }
}

namespace {

core::Array<vacomplex::Node*>
toVacNodes(const Workspace& workspace, core::ConstSpan<core::Id> elementIds) {
    core::Array<vacomplex::Node*> nodes;
    for (core::Id id : elementIds) {
        workspace::Element* element = workspace.find(id);
        if (element) {
            vacomplex::Node* node = element->vacNode();
            if (node && !nodes.contains(node)) {
                nodes.append(node);
            }
        }
    }
    return nodes;
}

// Same as above but also return the IDs of valid elements that are not VAC nodes.
//
core::Array<vacomplex::Node*> toVacNodes(
    const Workspace& workspace,
    core::ConstSpan<core::Id> elementIds,
    core::Array<core::Id>& otherElements) {

    core::Array<vacomplex::Node*> nodes;
    for (core::Id id : elementIds) {
        workspace::Element* element = workspace.find(id);
        if (element) {
            vacomplex::Node* node = element->vacNode();
            if (node) {
                if (!nodes.contains(node)) {
                    nodes.append(node);
                }
            }
            else {
                if (!otherElements.contains(id)) {
                    otherElements.append(id);
                }
            }
        }
    }
    return nodes;
}

void addVacNodes(
    const Workspace& workspace,
    const core::Array<vacomplex::Node*>& nodes,
    core::Array<core::Id>& result) {

    for (vacomplex::Node* node : nodes) {
        if (Element* element = workspace.findVacElement(node)) {
            core::Id id = element->id();
            if (!result.contains(id)) {
                result.append(id);
            }
        }
    }
}

} // namespace

void Workspace::softDelete(core::ConstSpan<core::Id> elementIds) {
    core::Array<vacomplex::Node*> nodes = toVacNodes(*this, elementIds);
    bool deleteIsolatedVertices = true;
    vacomplex::ops::softDelete(nodes, deleteIsolatedVertices);
}

core::Array<core::Id> Workspace::boundary(core::ConstSpan<core::Id> elementIds) const {
    core::Array<core::Id> result;
    core::Array<vacomplex::Node*> nodes = toVacNodes(*this, elementIds);
    addVacNodes(*this, vacomplex::boundary(nodes), result);
    return result;
}

core::Array<core::Id>
Workspace::outerBoundary(core::ConstSpan<core::Id> elementIds) const {
    core::Array<core::Id> result;
    core::Array<vacomplex::Node*> nodes = toVacNodes(*this, elementIds);
    addVacNodes(*this, vacomplex::outerBoundary(nodes), result);
    return result;
}

core::Array<core::Id> Workspace::star(core::ConstSpan<core::Id> elementIds) const {
    core::Array<core::Id> result;
    core::Array<vacomplex::Node*> nodes = toVacNodes(*this, elementIds);
    addVacNodes(*this, vacomplex::star(nodes), result);
    return result;
}

// Note: it should be equivalent here to use vacomplex::boundary() instead of
// vacomplex::closure(), but it's unclear which is faster: the computation of
// boundary is more complex, but the end array is smaller. In doubt, we simply
// use vacomplex::closure(), but still initialize the result with the input
// elementIds, since some of these may not be VAC nodes and need to stay
// selected.
//
core::Array<core::Id> Workspace::closure(core::ConstSpan<core::Id> elementIds) const {
    core::Array<core::Id> result(elementIds);
    core::Array<vacomplex::Node*> nodes = toVacNodes(*this, elementIds);
    addVacNodes(*this, vacomplex::closure(nodes), result);
    return result;
}

// Note: we intentionally use vacomplex::star() instead of opening() here
// as it is more efficient and gives the same end result since all input
// `elementsIds` are already included in `result`.
//
core::Array<core::Id> Workspace::opening(core::ConstSpan<core::Id> elementIds) const {
    core::Array<core::Id> result(elementIds);
    core::Array<vacomplex::Node*> nodes = toVacNodes(*this, elementIds);
    addVacNodes(*this, vacomplex::star(nodes), result);
    return result;
}

core::Array<core::Id> Workspace::connected(core::ConstSpan<core::Id> elementIds) const {
    core::Array<core::Id> result(elementIds);
    core::Array<vacomplex::Node*> nodes = toVacNodes(*this, elementIds);
    addVacNodes(*this, vacomplex::connected(nodes), result);
    return result;
}

core::Array<core::Array<core::Id>>
Workspace::connectedComponents(core::ConstSpan<core::Id> elementIds) const {

    // Separate into VAC nodes and other elements
    core::Array<core::Id> otherElements;
    core::Array<vacomplex::Node*> nodeElements =
        toVacNodes(*this, elementIds, otherElements);

    // Compute connected components of VAC nodes
    core::Array<core::Array<vacomplex::Node*>> connectedComponents =
        vacomplex::connectedComponents(nodeElements);

    // Convert to output IDs
    core::Array<core::Array<core::Id>> result;
    for (const core::Array<vacomplex::Node*>& nodes : connectedComponents) {
        core::Array<core::Id> ids;
        addVacNodes(*this, nodes, ids);
        result.append(std::move(ids));
    }

    // Add other elements as separate connect components
    for (core::Id id : otherElements) {
        result.append(core::Array<core::Id>(id, 1));
    }

    return result;
}

std::unordered_map<core::StringId, Workspace::ElementCreator>&
Workspace::elementCreators_() {
    static std::unordered_map<core::StringId, Workspace::ElementCreator>* instance_ =
        new std::unordered_map<core::StringId, Workspace::ElementCreator>();
    return *instance_;
}

void Workspace::removeElement_(Element* element) {
    VGC_ASSERT(element);
    bool removed = removeElement_(element->id());
    VGC_ASSERT(removed);
}

bool Workspace::removeElement_(core::Id id) {

    // Find the element with the given ID. Fast return if not found.
    //
    auto it = elements_.find(id);
    if (it == elements_.end()) {
        return false;
    }
    Element* element = it->second.get();

    // Update parent-child relationship between elements.
    //
    element->unparent();
    if (rootVacElement_ == element) {
        rootVacElement_ = nullptr;
    }

    // Remove from error list and pending update lists.
    //
    if (element->hasError()) {
        elementsWithError_.removeOne(element);
    }
    if (element->hasPendingUpdate()) {
        elementsToUpdateFromDom_.removeOne(element);
    }

    // Remove from the elements_ map.
    //
    // Note: we temporarily keep a unique_ptr before calling erase() since the
    // element's destructor can indirectly use elements_ via callbacks (e.g.
    // onVacNodeDestroyed_).
    //
    std::unique_ptr<Element> uptr = std::move(it->second);
    elements_.erase(it);

    // Update dependencies and execute callbacks.
    //
    while (!element->dependents_.isEmpty()) {
        Element* dependent = element->dependents_.pop();
        dependent->dependencies_.removeOne(element);
        dependent->onDependencyRemoved_(element);
        element->onDependentElementRemoved_(dependent);
    }

    // Finally destruct the element.
    //
    uptr.reset();
    return true;
}

void Workspace::clearElements_() {
    // Note: elements_.clear() indirectly calls onVacNodeDestroyed_() and thus fills
    // elementsToUpdateFromDom_. So it is important to clear the latter after the former.
    for (const auto& p : elements_) {
        Element* e = p.second.get();
        while (!e->dependents_.isEmpty()) {
            Element* dependent = e->dependents_.pop();
            dependent->dependencies_.removeOne(e);
            dependent->onDependencyRemoved_(e);
            e->onDependentElementRemoved_(dependent);
        }
    }
    elements_.clear();
    rootVacElement_ = nullptr;
    elementsWithError_.clear();
    elementsToUpdateFromDom_.clear();
}

void Workspace::setPendingUpdateFromDom_(Element* element) {
    if (!element->hasPendingUpdateFromDom_) {
        element->hasPendingUpdateFromDom_ = true;
        elementsToUpdateFromDom_.emplaceLast(element);
    }
}

void Workspace::clearPendingUpdateFromDom_(Element* element) {
    if (element->hasPendingUpdateFromDom_) {
        element->hasPendingUpdateFromDom_ = false;
        elementsToUpdateFromDom_.removeOne(element);
    }
}

void Workspace::fillVacElementListsUsingTagName_(
    Element* root,
    detail::VacElementLists& ce) const {

    namespace ds = dom::strings;

    Element* e = root->firstChild();
    Int depth = 1;

    while (e) {
        bool skipChildren = true;

        core::StringId tagName = e->domElement_->tagName();
        if (tagName == ds::vertex) {
            ce.keyVertices.append(e);
        }
        else if (tagName == ds::edge) {
            ce.keyEdges.append(e);
        }
        else if (tagName == ds::layer) {
            ce.groups.append(e);
            skipChildren = false;
        }

        iterDfsPreOrder(e, depth, root, skipChildren);
    }
}

namespace {

void debugVacRec(vacomplex::Node* node, Int depth) {
    std::string s;
    core::StringWriter sw(s);
    node->debugPrint(sw);
    VGC_DEBUG(LogVgcWorkspace, "{:>{}}{}", "", depth * 2, s);
    if (vacomplex::Group* group = node->toGroup()) {
        for (vacomplex::Node* child : *group) {
            debugVacRec(child, depth + 1);
        }
    }
}

} // namespace

void Workspace::debugPrintWorkspaceTree_() {
    visitDepthFirstPreOrder([](Element* e, Int depth) {
        VacElement* vacElement = e->toVacElement();
        if (vacElement) {
            vacomplex::Node* node = vacElement->vacNode();
            VGC_DEBUG(
                LogVgcWorkspace,
                "{:>{}}<{} id=\"{}\">(vacid: \"{}\")",
                "",
                depth * 2,
                e->tagName(),
                e->id(),
                (node ? node->id() : -1));
        }
        else {
            VGC_DEBUG(
                LogVgcWorkspace,
                "{:>{}}<{} id=\"{}\">",
                "",
                depth * 2,
                e->tagName(),
                e->id());
        }
    });
    if (rootVacElement_) {
        debugVacRec(rootVacElement_->vacNode(), 0);
    }
}

void Workspace::preUpdateDomFromVac_() {

    auto document = document_.lock();
    if (!document) {
        return;
    }

    // Check whether the Workspace is properly synchronized with the DOM before
    // we attempt to update the DOM from the VAC.
    //
    if (document->hasPendingDiff()) {
        VGC_ERROR(
            LogVgcWorkspace,
            "The topological complex has been edited while not being up to date with "
            "the latest changes in the document: the two may now be out of sync. "
            "This is probably caused by a missing document.emitPendingDiff().");
        flushDomDiff_();
        // TODO: rebuild from DOM instead of ignoring the pending diffs?
    }

    // Note: There is no need to do something like `isUpdatingDomFromVac =
    // true` here, because no signal is emitted from the DOM between
    // preUpdateDomFromVac_() and postUpdateDomFromVac_. Indeed, we only
    // call document->emitPendingDiff() in postUpdateDomFromVac_(), not
    // before.
}

void Workspace::postUpdateDomFromVac_() {

    // Now that we're done updating the DOM from the VAC, we emit the pending
    // DOM diff but ignore them. Indeed, the Workspace is the author of the
    // pending diff so there is no need to process them.
    //
    flushDomDiff_();

    // Inform the world that the Workspace content has changed.
    //
    changed().emit();

    // TODO: delay for batch VAC-to-DOM updates?
}

void Workspace::updateElementFromVac_(
    VacElement* element,
    vacomplex::NodeModificationFlags flags) {

    element->updateFromVac_(flags);
    // Note: VAC always has a correct state so this element should
    //       be errorless after being updated from VAC.
    element->status_ = ElementStatus::Ok;
}

void Workspace::rebuildDomFromWorkspaceTree_() {
    // todo later
    throw core::RuntimeError("not implemented");
}

void Workspace::onVacCellSamplingQualityChanged_(const vacomplex::Cell* cell) {
    VacElement* item = findVacElement(cell);
    if (VacKeyEdge* vacKeyEdge = dynamic_cast<VacKeyEdge*>(item)) {
        vacKeyEdge->dirtyPreJoinGeometry_();
    }
}

void Workspace::onVacNodesChanged_(const vacomplex::ComplexDiff& diff) {

    auto document = document_.lock();
    if (!document) {
        return;
    }

    // Process destroyed VAC nodes.
    //
    // Corresponding workspace elements are kept until the end of this function
    // but their vacNode_ pointer must be set to null.
    //
    // Note: we want to process deletions even if isUpdatingFromDom is true.
    // Indeed, if a VAC node has been destroyed as a side-effect of updating
    // the VAC from the DOM, it can mean that some Workspace elements should
    // now be mark as corrupted.
    //
    // Example scenario:
    //
    // 1. User deletes a <vertex> element in the DOM Editor, despite
    //    an existing <edge> element using it as end vertex.
    //
    // 2. This modifies the dom::Document, calls doc->emitPendingDiff(),
    //    which in turn calls Workspace::onDocumentDiff().
    //
    // 3. The Workspace retrieves the corresponding vertexElement Workspace element
    //    and vertexNode VAC Node.
    //
    // 4. The Workspace destroys and unregisters the vertexElement.
    //
    // 5. The Workspace calls ops::hardDelete(vertexNode)
    //
    // 6. The VAC diff contains destroyed node ids:
    //    - vertexNodeId  -> destroyed explicitly:     OK
    //    - edgeNodeId    -> destroyed as side-effect: now corrupted
    //
    if (isUpdatingVacFromDom_) {
        for (const auto& info : diff.destroyedNodes()) {
            VacElement* vacElement = findVacElement(info.nodeId());
            if (vacElement) {
                vacElement->unsetVacNode(info.nodeId());
                setPendingUpdateFromDom_(vacElement);
            }
        }
        return;
    }
    std::unordered_map<core::Id, VacElement*> workspaceItemsToDestroy;
    for (const auto& info : diff.destroyedNodes()) {
        VacElement* vacElement = findVacElement(info.nodeId());
        if (vacElement) {
            vacElement->unsetVacNode(info.nodeId());
            workspaceItemsToDestroy.emplace(info.nodeId(), vacElement);
        }
    }

    auto findWorkspaceItemFromVacNodeId = //
        [this, &workspaceItemsToDestroy](core::Id vacNodeId) {
            VacElement* element = this->findVacElement(vacNodeId);
            if (!element) {
                auto it = workspaceItemsToDestroy.find(vacNodeId);
                if (it != workspaceItemsToDestroy.end()) {
                    element = it->second;
                }
            }
            return element;
        };

    preUpdateDomFromVac_();

    // Process created vac nodes
    //
    core::Array<VacElement*> createdElements;
    createdElements.reserve(diff.createdNodes().length());
    for (const auto& info : diff.createdNodes()) {
        vacomplex::Node* node = info.node();

        // TODO: check id conflict

        // TODO: add constructors expecting operationSourceNodes

        // Create the workspace element
        std::unique_ptr<Element> u = {};
        if (node->isGroup()) {
            //vacomplex::Group* group = node->toGroup();
            u = makeUniqueElement<Layer>(this);
        }
        else {
            vacomplex::Cell* cell = node->toCell();
            switch (cell->cellType()) {
            case vacomplex::CellType::KeyVertex:
                u = makeUniqueElement<VacKeyVertex>(this);
                break;
            case vacomplex::CellType::KeyEdge:
                u = makeUniqueElement<VacKeyEdge>(this);
                break;
            case vacomplex::CellType::KeyFace:
                u = makeUniqueElement<VacKeyFace>(this);
                break;
            case vacomplex::CellType::InbetweenVertex:
            case vacomplex::CellType::InbetweenEdge:
            case vacomplex::CellType::InbetweenFace:
                break;
            }
        }
        if (!u) {
            // TODO: error ?
            continue;
        }

        VacElement* element = u->toVacElement();
        if (!element) {
            // TODO: error ?
            continue;
        }

        // By default insert is as last child of root.
        // There should be a NodeInsertionInfo in the diff that will
        // let us move it to its final destination later in this function.
        rootVacElement_->appendChild(element);

        // Create the DOM element (as last child of root too)

        dom::ElementPtr domElement = dom::Element::create(
            document->rootElement(), element->domTagName().value(), nullptr);
        const core::Id id = domElement->internalId();

        const auto& p = elements_.emplace(id, std::move(u));
        if (!p.second) {
            // TODO: throw ?
            continue;
        }

        element->domElement_ = domElement.get();
        element->id_ = id;
        element->setVacNode(node);

        createdElements.append(element);
    }

    // Process transient vac nodes
    //
    for (const auto& info : diff.transientNodes()) {
        core::Id nodeId = info.nodeId();

        // TODO: check id conflict

        // TODO: add constructors expecting operationSourceNodes

        // Create the workspace element
        std::unique_ptr<Element> u = makeUniqueElement<TransientVacElement>(this);

        VacElement* element = u->toVacElement();
        if (!element) {
            // TODO: error ?
            continue;
        }

        // By default insert is as last child of root.
        // There should be a NodeInsertionInfo in the diff that will
        // let us move it to its final destination later in this function.
        rootVacElement_->appendChild(element);

        dom::ElementPtr domElement =
            dom::Element::create(document->rootElement(), "transient", nullptr);
        const core::Id id = domElement->internalId();

        // Keep alive this transient node until the end of onVacNodesChanged_().
        const auto& p = elements_.emplace(id, std::move(u));
        bool wasEmplaced = p.second;
        if (!wasEmplaced) {
            // ID conflict => the transient element was destroyed
            // TODO: throw ?
            continue;
        }
        workspaceItemsToDestroy.emplace(nodeId, element);

        element->domElement_ = domElement.get();
        element->id_ = id;
        element->status_ = ElementStatus::Ok;
    }

    // Process insertions
    for (const auto& info : diff.insertions()) {
        VacElement* vacElement = findWorkspaceItemFromVacNodeId(info.nodeId());
        if (!vacElement) {
            VGC_ERROR(
                LogVgcWorkspace,
                "No matching workspace::VacElement found for "
                "nodeInsertionInfo->nodeId().");
            continue;
        }
        dom::Element* domElement = vacElement->domElement();

        VacElement* vacParentElement = findWorkspaceItemFromVacNodeId(info.newParentId());
        if (!vacParentElement) {
            VGC_ERROR(
                LogVgcWorkspace,
                "No matching workspace::VacElement found for "
                "nodeInsertionInfo->newParentId().");
            continue;
        }
        dom::Element* domParentElement = vacParentElement->domElement();

        switch (info.type()) {
        case vacomplex::NodeInsertionType::BeforeSibling: {
            VacElement* vacSiblingElement =
                findWorkspaceItemFromVacNodeId(info.newSiblingId());
            if (!vacSiblingElement) {
                VGC_ERROR(
                    LogVgcWorkspace,
                    "No matching workspace::VacElement found for "
                    "nodeInsertionInfo->newSiblingId().");
                continue;
            }
            dom::Element* domSiblingElement = vacSiblingElement->domElement();
            vacParentElement->insertChildUnchecked(vacSiblingElement, vacElement);
            domParentElement->insertChild(domSiblingElement, domElement);
            break;
        }
        case vacomplex::NodeInsertionType::AfterSibling: {
            VacElement* vacSiblingElement =
                findWorkspaceItemFromVacNodeId(info.newSiblingId());
            if (!vacSiblingElement) {
                VGC_ERROR(
                    LogVgcWorkspace,
                    "No matching workspace::VacElement found for "
                    "nodeInsertionInfo->newSiblingId().");
                continue;
            }
            dom::Element* domSiblingElement = vacSiblingElement->domElement();
            vacParentElement->insertChildUnchecked(
                vacSiblingElement->nextSibling(), vacElement);
            domParentElement->insertChild(domSiblingElement->nextSibling(), domElement);
            break;
        }
        case vacomplex::NodeInsertionType::FirstChild: {
            vacParentElement->insertChildUnchecked(
                vacParentElement->firstChild(), vacElement);
            domParentElement->insertChild(domParentElement->firstChild(), domElement);
            break;
        }
        case vacomplex::NodeInsertionType::LastChild: {
            vacParentElement->insertChildUnchecked(nullptr, vacElement);
            domParentElement->insertChild(nullptr, domElement);
            break;
        }
        }
    }

    // Update created vac nodes
    //
    for (VacElement* element : createdElements) {
        updateElementFromVac_(element, vacomplex::NodeModificationFlag::All);
    }

    // Process modified vac nodes
    //
    for (const auto& info : diff.modifiedNodes()) {
        VacElement* vacElement = findWorkspaceItemFromVacNodeId(info.nodeId());
        if (!vacElement) {
            VGC_ERROR(LogVgcWorkspace, "Unexpected vacomplex::Node");
            // TODO: recover from error by creating the Cell in workspace and DOM ?
            continue;
        }
        updateElementFromVac_(vacElement, info.flags());
    }

    // Process destroyed vac nodes
    //
    // XXX What if node is the root node?
    //
    // Currently, in the Workspace(document) constructor, we first create a
    // vacomplex::Complex (which creates a root node), then call
    // rebuildFromDom(), which first clears the Complex (destructing the root
    // node), then creates the nodes including the root node. Therefore, this
    // function is called in the Workspace(document) constructor, which is
    // probably not ideal.
    //
    // Get Workspace element corresponding to the destroyed VAC node. Nothing
    // to do if there is no such element.
    //
    // Note: transient vac nodes are included.
    //
    for (auto [id, vacElement] : workspaceItemsToDestroy) {
        VGC_UNUSED(id);

        // Delete the corresponding DOM element if any.
        //
        dom::Element* domElement = vacElement->domElement();
        if (domElement) {
            domElement->remove();
        }

        // Delete the Workspace element.
        //
        removeElement_(vacElement);
    }

    postUpdateDomFromVac_();
}

void Workspace::onDocumentDiff_(const dom::Diff& diff) {
    if (numDocumentDiffToSkip_ > 0) {
        --numDocumentDiffToSkip_;
    }
    else {
        updateVacFromDom_(diff);
    }
}

// Flushing ensures that the DOM doesn't contain pending diff, by emitting
// but ignoring them.
//
void Workspace::flushDomDiff_() {
    if (auto document = document_.lock()) {
        if (document->hasPendingDiff()) {
            ++numDocumentDiffToSkip_;
            document->emitPendingDiff();
        }
    }
}

// This method creates the workspace element corresponding to a given
// DOM element, but without initializing it yet (that is, it doesn't
// create the corresponding VAC element)
//
// Initialization is perform later, by calling updateElementFromDom(element).
//
Element*
Workspace::createAppendElementFromDom_(dom::Element* domElement, Element* parent) {
    if (!domElement) {
        return nullptr;
    }

    std::unique_ptr<Element> createdElementPtr = {};
    auto& creators = elementCreators_();
    auto it = creators.find(domElement->tagName());
    if (it != creators.end()) {
        auto& creator = it->second;
        createdElementPtr = std::invoke(creator, this);
        if (!createdElementPtr) {
            VGC_ERROR(
                LogVgcWorkspace,
                "Element creator for \"{}\" failed to create the element.",
                domElement->tagName());
            // XXX throw or fallback to UnsupportedElement or nullptr ?
            createdElementPtr = std::make_unique<UnsupportedElement>(this);
        }
    }
    else {
        createdElementPtr = std::make_unique<UnsupportedElement>(this);
    }

    core::Id id = domElement->internalId();
    Element* createdElement = createdElementPtr.get();
    bool emplaced = elements_.try_emplace(id, std::move(createdElementPtr)).second;
    if (!emplaced) {
        // TODO: should probably throw
        return nullptr;
    }
    createdElement->domElement_ = domElement;
    createdElement->id_ = id;

    if (parent) {
        parent->appendChild(createdElement);
    }

    setPendingUpdateFromDom_(createdElement);

    return createdElement;
}

namespace {

// Updates parent.
// Assumes (parent == it->parent()).
dom::Element* rebuildTreeFromDomIter(Element* it, Element*& parent) {
    dom::Element* e = it->domElement();
    dom::Element* next = nullptr;
    // depth first
    next = e->firstChildElement();
    if (next) {
        parent = it;
        return next;
    }
    // breadth next
    while (e) {
        next = e->nextSiblingElement();
        if (next) {
            return next;
        }
        // go up
        if (!parent) {
            return nullptr;
        }
        e = parent->domElement();
        parent = parent->parent();
    }
    return next;
}

} // namespace

void Workspace::rebuildWorkspaceTreeFromDom_() {

    auto document = document_.lock();
    auto vac = vac_.lock();
    VGC_ASSERT(document);
    VGC_ASSERT(vac);

    // reset tree
    clearElements_();

    // reset VAC
    {
        core::BoolGuard bgVac(isUpdatingVacFromDom_);
        vac->clear();
        //vac->emitPendingDiff();
    }

    flushDomDiff_();

    dom::Element* domVgcElement = document->rootElement();
    if (!domVgcElement || domVgcElement->tagName() != dom::strings::vgc) {
        return;
    }

    Element* vgcElement = createAppendElementFromDom_(domVgcElement, nullptr);
    VGC_ASSERT(vgcElement);
    rootVacElement_ = vgcElement->toVacElement();
    VGC_ASSERT(rootVacElement_);

    Element* p = nullptr;
    Element* e = rootVacElement_;
    dom::Element* domElement = rebuildTreeFromDomIter(e, p);
    while (domElement) {
        e = createAppendElementFromDom_(domElement, p);
        domElement = rebuildTreeFromDomIter(e, p);
    }

    // children should already be in the correct order.
}

void Workspace::rebuildVacFromWorkspaceTree_() {

    auto vac = vac_.lock();
    VGC_ASSERT(vac);
    VGC_ASSERT(rootVacElement_);

    core::BoolGuard bgVac(isUpdatingVacFromDom_);

    // reset vac
    vac->clear();
    vac->resetRoot();
    rootVacElement_->setVacNode(vac->rootGroup());

    Element* root = rootVacElement_;
    Element* element = root->firstChild();
    Int depth = 1;
    while (element) {
        updateElementFromDom(element);
        iterDfsPreOrder(element, depth, root);
    }

    updateVacChildrenOrder_();
}

void Workspace::updateVacChildrenOrder_() {
    // todo: sync children order in all groups
    Element* root = rootVacElement_;
    Element* element = root;

    Int depth = 0;
    while (element) {
        vacomplex::Node* node = element->vacNode();
        if (node) {
            if (node->isGroup()) {
                vacomplex::Group* group = static_cast<vacomplex::Group*>(node);
                // synchronize first common child
                VacElement* childVacElement = element->firstChildVacElement();
                while (childVacElement) {
                    if (childVacElement->vacNode()) {
                        vacomplex::ops::moveToGroup(
                            childVacElement->vacNode(), group, group->firstChild());
                        break;
                    }
                    childVacElement = childVacElement->nextSiblingVacElement();
                }
            }

            if (node->parentGroup()) {
                // synchronize next sibling element's node as node's next sibling
                VacElement* nextSiblingVacElement = element->nextSiblingVacElement();
                while (nextSiblingVacElement) {
                    vacomplex::Node* nextSiblingElementNode =
                        nextSiblingVacElement->vacNode();
                    if (nextSiblingElementNode) {
                        vacomplex::ops::moveToGroup(
                            nextSiblingElementNode,
                            node->parentGroup(),
                            node->nextSibling());
                        break;
                    }
                    nextSiblingVacElement =
                        nextSiblingVacElement->nextSiblingVacElement();
                }
            }
        }

        iterDfsPreOrder(element, depth, root);
    }
}

//bool Workspace::haveKeyEdgeBoundaryPathsChanged_(Element* e) {
//    vacomplex::Node* node = e->vacNode();
//    if (!node) {
//        return false;
//    }
//
//    namespace ds = dom::strings;
//    dom::Element* const domElem = e->domElement();
//
//    Element* ev0 = getElementFromPathAttribute(domElem, ds::startvertex, ds::vertex);
//    Element* ev1 = getElementFromPathAttribute(domElem, ds::endvertex, ds::vertex);
//
//    vacomplex::Node* node = vacNode();
//    vacomplex::KeyEdge* kv = node ? node->toCellUnchecked()->toKeyEdgeUnchecked() : nullptr;
//    if (!kv) {
//        return false;
//    }
//
//    if (ev0) {
//        if (ev0->vacNode() != kv->startVertex()) {
//            return true;
//        }
//    }
//    else if (kv->startVertex()) {
//        return true;
//    }
//    if (ev1) {
//        if (ev1->vacNode() != kv->endVertex()) {
//            return true;
//        }
//    }
//    else if (kv->endVertex()) {
//        return true;
//    }
//
//    return false;
//}

void Workspace::updateVacFromDom_(const dom::Diff& diff) {

    auto document = document_.lock();
    auto vac = vac_.lock();
    VGC_ASSERT(document);
    VGC_ASSERT(vac);

    if (vac->isOperationInProgress()) {
        throw core::LogicError("VAC and DOM have been concurrently modified (forgot to "
                               "call workspace.sync() after modifying the DOM?).");

        // Alternatively, instead of a LogicError, maybe we could destroy the
        // current vac() and reconstruct it from scratch from the DOM (that is,
        // consider the DOM to be our primary source of trust).
    }

    core::BoolGuard bgVac(isUpdatingVacFromDom_);

    // impl goal: we want to keep as much cached data as possible.
    //            we want the VAC to be valid -> using only its operators
    //            limits bugs to their implementation.

    bool hasModifiedPaths =
        (diff.removedNodes().length() > 0) || (diff.reparentedNodes().size() > 0);
    bool hasNewPaths = (diff.createdNodes().length() > 0);

    std::set<Element*> parentsToOrderSync;

    // First, we remove what has to be removed.
    //
    // Note that if `element` is a VacElement, then:
    // - removeElement_(element) calls vacomplex::ops::hardDelete(node),
    // - which may destroy other VAC nodes,
    // - which will invoke onVacNodeDestroyed(otherNodeId).
    //
    // Note: it is important not to have an operations group around
    // this code block, since we want to receive the vacomplex diff
    // after each individual deletion, since it may have deleted more
    // cells.
    //
    // Example scenario: using the DOM editor, a user deletes a vertex and
    // modifies (but not delete) its incident edge (for example, making it a
    // closed edge). As part of the code block below, a call to
    // ops::hardDelete(vertex) will be made, deleting both the vertex and the
    // edge. At the end of the deletion of the vertex, the workspace will be
    // made aware of the deletion of the edge (see onVacNodesChanged_()),
    // setting to nullptr a now-dangling pointer in the corresponding
    // VacKeyEdge. This ensures that we can safely proceed, in the operations
    // group below, to call methods such updateElementFromDom(edge) (which will
    // recreate the edge if possible).
    //
    for (dom::Node* node : diff.removedNodes()) {
        dom::Element* domElement = dom::Element::cast(node);
        if (!domElement) {
            continue;
        }
        Element* element = find(domElement);
        if (element) {
            Element* parent = element->parent();
            VGC_ASSERT(parent);
            // reparent children to element's parent
            for (Element* child : *element) {
                parent->appendChild(child);
            }
            removeElement_(element);
        }
    }

    // Creates a group of operations, so that post-operation computations are
    // done only once at the end of this function. For example, if we update
    // both an edge geometry and a vertex position, we do not want to perform
    // snapping between the update of the edge and the update of the vertex.
    //
    vacomplex::ScopedOperationsGroup operationsGroup(vac.get());

    // Create new elements, but for now uninitialized and in a potentially
    // incorrect child ordering.
    //
    for (dom::Node* node : diff.createdNodes()) {
        dom::Element* domElement = dom::Element::cast(node);
        if (!domElement) {
            continue;
        }
        dom::Element* domParentElement = domElement->parentElement();
        if (!domParentElement) {
            continue;
        }
        Element* parent = find(domParentElement);
        if (!parent) {
            // XXX warn ? createdNodes should be in valid build order
            //            and <vgc> element should already exist.
            continue;
        }
        createAppendElementFromDom_(domElement, parent);
        parentsToOrderSync.insert(parent);
    }

    // Collect all parents with reordered children.
    //
    for (dom::Node* node : diff.reparentedNodes()) {
        dom::Element* domElement = dom::Element::cast(node);
        if (!domElement) {
            continue;
        }
        dom::Element* domParentElement = domElement->parentElement();
        if (!domParentElement) {
            continue;
        }
        Element* parent = find(domParentElement);
        if (parent) {
            parentsToOrderSync.insert(parent);
        }
    }
    for (dom::Node* node : diff.childrenReorderedNodes()) {
        dom::Element* domElement = dom::Element::cast(node);
        if (!domElement) {
            continue;
        }
        Element* element = find(domElement);
        if (!element) {
            // XXX error ?
            continue;
        }
        parentsToOrderSync.insert(element);
    }

    // Update children order between workspace elements.
    //
    for (Element* element : parentsToOrderSync) {
        Element* child = element->firstChild();
        dom::Element* domChild = element->domElement()->firstChildElement();
        while (domChild) {
            if (!child || child->domElement() != domChild) {
                Element* missingChild = find(domChild);
                while (!missingChild) {
                    domChild = domChild->nextSiblingElement();
                    if (!domChild) {
                        break;
                    }
                    missingChild = find(domChild);
                }
                if (!domChild) {
                    break;
                }
                element->insertChildUnchecked(child, missingChild);
                child = missingChild;
            }
            child = child->nextSibling();
            domChild = domChild->nextSiblingElement();
        }
    }

    if (hasNewPaths || hasModifiedPaths) {
        // Flag all elements with error for update.
        for (Element* element : elementsWithError_) {
            setPendingUpdateFromDom_(element);
        }
    }

    if (hasModifiedPaths) {
        // Update everything for now.
        // TODO: An element dependent on a Path should have it in its dependencies
        // so we could force path reevaluation to the dependents of an element that moved,
        // as well as errored elements.
        Element* root = rootVacElement_;
        Element* element = root;
        Int depth = 0;
        while (element) {
            setPendingUpdateFromDom_(element);
            iterDfsPreOrder(element, depth, root);
        }
    }
    else {
        // Otherwise we update the elements flagged as modified
        for (const auto& it : diff.modifiedElements()) {
            Element* element = find(it.first);
            // If the element has already an update pending it will be
            // taken care of in the update loop further below.
            if (element) {
                setPendingUpdateFromDom_(element);
                // TODO: pass the set of modified attributes ids to the Element
            }
        }
    }

    // Now that all workspace elements are created (or removed), we finally
    // update all the elements by calling their updateFromDom() virtual method,
    // which creates their corresponding VAC node if any, and transfers all the
    // attributes from the DOM element to the workspace element and/or VAC
    // node.
    //
    while (!elementsToUpdateFromDom_.isEmpty()) {
        // There is no need to pop the element since updateElementFromDom is
        // in charge of removing it from the list when updated.
        Element* element = elementsToUpdateFromDom_.last();
        updateElementFromDom(element);
    }

    // Update children order between VAC nodes.
    //
    updateVacChildrenOrder_();

    // Update Version ID and notify that the workspace changed.
    //
    lastSyncedDomVersionId_ = document->versionId();
    changed().emit();
}

//void Workspace::updateTreeAndDomFromVac_(const vacomplex::Diff& diff) {
//
//    VGC_UNUSED(diff);
//
//    if (!document_) {
//        VGC_ERROR(LogVgcWorkspace, "DOM is null.")
//        return;
//    }
//
//    detail::ScopedTemporaryBoolSet bgDom(isCreatingDomElementsFromVac_);
//
//    // todo later
//    throw core::RuntimeError("not implemented");
//}

} // namespace vgc::workspace
