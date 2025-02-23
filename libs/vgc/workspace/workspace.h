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

#ifndef VGC_WORKSPACE_WORKSPACE_H
#define VGC_WORKSPACE_WORKSPACE_H

#include <map>
#include <unordered_map>

#include <vgc/core/animtime.h>
#include <vgc/core/flags.h>
#include <vgc/core/id.h>
#include <vgc/core/object.h>
#include <vgc/core/span.h>
#include <vgc/dom/document.h>
#include <vgc/graphics/engine.h>
#include <vgc/vacomplex/complex.h>
#include <vgc/workspace/api.h>
#include <vgc/workspace/element.h>

namespace vgc::workspace {

VGC_DECLARE_OBJECT(Workspace);

namespace detail {

struct VacElementLists;

class ScopedUndoGroup {
public:
    VGC_DISABLE_COPY(ScopedUndoGroup);

    ScopedUndoGroup() = default;

    explicit ScopedUndoGroup(core::UndoGroup* undoGroup)
        : undoGroup_(undoGroup) {
    }

    ScopedUndoGroup(ScopedUndoGroup&& other) {
        undoGroup_ = other.undoGroup_;
        other.undoGroup_ = nullptr;
    }

    ScopedUndoGroup& operator=(ScopedUndoGroup&& other) {
        if (this != &other) {
            undoGroup_ = other.undoGroup_;
            other.undoGroup_ = nullptr;
        }
        return *this;
    }

    ~ScopedUndoGroup() {
        if (auto undoGroup = undoGroup_.lock()) {
            undoGroup->close();
        }
    }

private:
    core::UndoGroupWeakPtr undoGroup_;
};

VGC_WORKSPACE_API
ScopedUndoGroup createScopedUndoGroup(Workspace* workspace, core::StringId name);

} // namespace detail

/// \class vgc::workspace::Workspace
/// \brief High-level interface to manipulate and render a vector graphics document.
///
/// A vector graphics document can be described as a DOM (`dom::Document`),
/// providing a simple low-level representation which is very useful for
/// serialization, undo/redo, or low-level editing in a DOM editor.
///
/// However, the DOM representation by itself does not provide any means to
/// render the scene, nor convenient methods to edit the underlying topological
/// objects described in the DOM. For such use cases, you can use a
/// `Workspace`.
///
/// A workspace takes as input a given DOM (`dom::Document`) and creates two
/// other parallel tree-like structures which are all kept synchronized:
///
/// 1. A topological complex (`vacomplex::Complex`), representing the explicit
///    or implicit vertices, edges, and faces described in the DOM.
///
/// 2. A workspace tree, unifying both the topological complex and the DOM.
///
/// By visiting the workspace tree, you can iterate not only on all the
/// elements in the DOM (including those not in the topological complex, e.g.,
/// text), but also on all the elements in the topological complex (including
/// those not in the DOM, e.g., implicit vertices, edges, and faces).
///
/// The elements in the workspace tree (`workspace::Element`) store pointers to
/// their corresponding `dom::Element` (if any), and their corresponding
/// `vacomplex::Node` (if any).
///
/// The elements in the workspace tree also store all the graphics resources
/// required to render the vector graphics document. These graphics resources
/// are computed from the base geometry provided by `vacomplex::Complex`, on
/// top of which is applied styling and compositing. For example, the workspace
/// is responsible for the computation of edge joins.
///
class VGC_WORKSPACE_API Workspace : public core::Object {
private:
    VGC_OBJECT(Workspace, core::Object)

    Workspace(CreateKey, dom::DocumentPtr document);

    void onDestroyed() override;

public:
    /// Creates a `Workspace` operating on the given `document`.
    ///
    static WorkspacePtr create(dom::DocumentPtr document);

    /// Returns the DOM that this workspace is operating on.
    ///
    /// \sa `vac()`.
    ///
    dom::DocumentWeakPtr document() const {
        return document_;
    }

    /// Returns the topological complex corresponding to the DOM that this
    /// workspace is operating on.
    ///
    /// This topological complex stores all the explicit and implicit vertices,
    /// edges, and faces described in the DOM.
    ///
    /// You can operate on this topological complex by using the topological
    /// operators available in the `vacomplex::ops` namespace. The DOM is
    /// always automatically updated to reflect these changes.
    ///
    /// However, note that modifications of the DOM do not cause an automatic
    /// update of the topological complex. After editing the DOM, you must
    /// explicitly call `workspace->sync()` or `document->emitPendingDiff()` in
    /// order to update the topological complex. This design protects against
    /// unsafe retroaction loops, improves performance, and makes it possible
    /// for the DOM to be temporarilly in an invalid state (topologically
    /// speaking) during a sequence of multiple edits from one valid state to
    /// another valid state.
    ///
    /// \sa `document()`.
    ///
    vacomplex::ComplexWeakPtr vac() const {
        return vac_;
    }

    /// If the `document()` of this workspace has enabled support for undo/redo
    /// via a `core::History()`, this function returns this history.
    ///
    /// Otherwise, returns `nullptr`.
    ///
    core::History* history() const {
        if (auto document = document_.lock()) {
            return document->history();
        }
        else {
            return nullptr;
        }
    }

    /// This signal is emitted whenever the workspace changes, either
    /// as a result of the DOM changing, or the topological complex
    /// changing.
    ///
    VGC_SIGNAL(changed);

    /// Returns the root workspace element, that is, the workspace element
    /// corresponding to the `<vgc>` root DOM element.
    ///
    Element* vgcElement() const {
        return rootVacElement_;
    }

    /// Returns the workspace element corresponding to the given ID, if any.
    ///
    /// Returns `nullptr` if no element corresponds to this ID, or if the element
    /// is not of the given `ElementType`.
    ///
    template<typename ElementType = Element>
    ElementType* find(core::Id elementId) const {
        auto it = elements_.find(elementId);
        return it != elements_.end() ? dynamic_cast<ElementType*>(it->second.get())
                                     : nullptr;
    }

    /// Returns the workspace element corresponding to the given DOM element, if
    /// any.
    ///
    /// Returns `nullptr` if no element corresponds to the given DOM element.
    /// This can happen if the DOM still has pending changes that have not been
    /// synchronized. If you call this function just after `sync()`, there
    /// should normally always be a `workspace::Element` corresponding to any
    /// `dom::Element`.
    ///
    Element* find(const dom::Element* element) const {
        return element ? find(element->internalId()) : nullptr;
    }

    /// Returns the workspace element of subtype `workspace::VacElement`
    /// corresponding to the given topological node ID (that is,
    /// `vacomplex::Node::id()`), if any.
    ///
    /// Returns `nullptr` if no `workspace::VacElement` corresponds to the
    /// given topological node ID.
    ///
    VacElement* findVacElement(core::Id nodeId) const {
        auto it = elementByVacInternalId_.find(nodeId);
        return it != elementByVacInternalId_.end() ? it->second : nullptr;
    }

    /// Returns the workspace element of subtype `workspace::VacElement`
    /// corresponding to the given topological `node`, if any.
    ///
    /// Under most circumstances, this function shouldn't return `nullptr` as
    /// long as the given `node` is non-null and is part of the topological
    /// complex managed by this workspace (i.e., if `node->vac() ==
    /// workspace->vac()`).
    ///
    /// However, this function might still return `nullptr` if it called as
    /// part of a slot connected to the `vacomplex::Complex::nodeCreated()`
    /// signal, if such slot is called before the workspace's own slot
    /// performing the synchronization between the topological complex and the
    /// workspace tree.
    ///
    VacElement* findVacElement(const vacomplex::Node* node) const {
        // XXX: warning if returns nullptr despite `node` being non-null?
        return node ? findVacElement(node->id()) : nullptr;
    }

    /// Returns the vacomplex cell corresponding to the given item ID, if any.
    ///
    /// Returns `nullptr` if no cell corresponds to this ID, or if the cell is
    /// not of the given `CellType`.
    ///
    template<typename CellType = vacomplex::Cell>
    CellType* findCellByItemId(core::Id itemId) const {
        Element* element = find(itemId);
        if (!element) {
            return nullptr;
        }
        vacomplex::Cell* cell = element->vacCell();
        if (!cell) {
            return nullptr;
        }
        return vacomplex::dynamic_cell_cast<CellType>(cell);
    }

    /// Returns the DOM element corresponding to the given vacomplex node, if any.
    ///
    /// Returns `nullptr` if no DOM element corresponds to this node.
    ///
    dom::Element* findDomElement(const vacomplex::Node* node) const {
        workspace::Element* item = findVacElement(node);
        return item ? item->domElement() : nullptr;
    }

    /// Explicitly synchronizes the DOM, workspace tree, and topological complex
    /// together.
    ///
    void sync();

    /// Rebuilds the workspace tree and the topological complex from scratch, based
    /// on the current state of the DOM.
    ///
    /// This function is useful temporarily while the implementation of this
    /// class is not complete. For example, we currently do not properly
    /// support path updates from the DOM, so in case of id/name changes, the
    /// `sync()` function wouldn't work properly and it is required to rebuild
    /// from scratch.
    ///
    void rebuildFromDom();

    /// Requests the workspace to update a specific workspace element (and its
    /// corresponding topological node, if any) based on its current
    /// description in the DOM.
    ///
    /// This function is meant to be called in reimplementations of
    /// `Element::updateFromDom()`, whenever another element must be updated
    /// first. For example, the implementation of `VacKeyEdge::updateFromDom()`
    /// calls `workspace->updateElement(vertex)` for each of its start and end
    /// vertices.
    ///
    /// If a cyclic update dependency is detected, then this function emits an
    /// error and does not perform the update.
    ///
    // XXX make it instead a (non-virtual) protected method of Element, e.g.,
    // updateDependencyFromDom(Element* dependency)? Or perhaps
    // Element::updateFromDom() can be renamed Element::onUpdateFromDom(), and
    // this function be Element::updateFromDom(Element* dependency).
    //
    bool updateElementFromDom(Element* element);

    /// Resolves the path stored in the attribute `attrName` of `domElement`,
    /// and returns its corresponding workspace element, if any.
    ///
    /// If `tagNameFilter` is not empty, and the tag name of the found element
    /// is not equal to `tagNameFilter`, then this function emits a warning and
    /// returns `nullptr`.
    ///
    /// Throws an exception if the attribute does not exist, or exist but is
    /// not of type `dom::ValueType::Path` or `dom::ValueType::NoneOrPath`.
    ///
    /// The behavior is undefined if `domElement` is null.
    ///
    /// Example:
    ///
    /// ```cpp
    /// std::optional<Element*> startVertexWorkspaceElement = workspace->getElementFromPathAttribute(
    ///     edgeDomElement,
    ///     dom::strings::startvertex,
    ///     dom::strings::vertex);
    /// ```
    ///
    std::optional<Element*> getElementFromPathAttribute(
        const dom::Element* domElement,
        core::StringId attrName,
        core::StringId tagNameFilter = {}) const;

    /// Traverses all elements in the workspace tree in a depth-first order.
    ///
    /// For each visited element, `preOrderFn(element, depth)` is called before
    /// visiting any of its children.
    ///
    void visitDepthFirstPreOrder(const std::function<void(Element*, Int)>& preOrderFn);

    /// Traverses all elements in the workspace tree in a depth-first order.
    ///
    /// For each visited element, `preOrderFn(element, depth)` is called before
    /// visiting any of its children, and `postOrderFn(element, depth)` is called
    /// after having visited all of its children.
    ///
    /// If `preOrderFn(element, depth)` returns false, then the children of
    /// `element` are not visited, allowing you to skip subtrees.
    ///
    void visitDepthFirst(
        const std::function<bool(Element*, Int)>& preOrderFn,
        const std::function<void(Element*, Int)>& postOrderFn);

    /// Moves the given elements (and potentially their boundary) towards the
    /// top of the stacking order.
    ///
    void bringForward(core::ConstSpan<core::Id> elementIds, core::AnimTime t);

    /// Moves the given elements (and potentially their boundary/star) towards
    /// the bottom of the stacking order.
    ///
    void sendBackward(core::ConstSpan<core::Id> elementIds, core::AnimTime t);

    /// Moves the given elements (and potentially their boundary) to the
    /// top of the stacking order.
    ///
    void bringToFront(core::ConstSpan<core::Id> elementIds, core::AnimTime t);

    /// Moves the given elements (and potentially their boundary/star) to
    /// the bottom of the stacking order.
    ///
    void sendToBack(core::ConstSpan<core::Id> elementIds, core::AnimTime t);

    /// Performs a glue operation on the given elements.
    ///
    /// This is both a geometrical and topological operation.
    ///
    /// Currently, the following sets of elements are supported:
    /// - Two or more key vertices.
    /// - Exactly two key edges.
    ///
    /// This function does nothing if the given elements are not
    /// one of the above supported sets of elements.
    ///
    core::Id glue(core::ConstSpan<core::Id> elementIds);

    /// Performs an unglue operation on the given elements.
    ///
    /// This is both a geometrical and topological operation.
    ///
    /// This function supports ungluing an arbitrary number of key vertices and
    /// key edges.
    ///
    core::Array<core::Id> unglue(core::ConstSpan<core::Id> elementIds);

    /// Performs cut-glue face operation on the given elements (1 face, 1 edge).
    ///
    /// This is both a geometrical and topological operation.
    ///
    bool cutGlueFace(core::ConstSpan<core::Id> elementIds);

    /// Performs uncut operations on the given elements.
    ///
    /// This is both a geometrical and topological operation.
    ///
    /// This function supports uncutting an arbitrary number of key vertices
    /// or key edges.
    ///
    core::Array<core::Id>
    simplify(core::ConstSpan<core::Id> elementIds, bool smoothJoins);

    /// Computes the geometric intersection between the given `elementIds` and
    /// all the objects in their parent group, then cut them accordingly.
    ///
    /// This currently only works for selected edges.
    ///
    /// Returns the new objects resulting from cutting the given `elementIds`.
    ///
    core::Array<core::Id> intersectWithGroup(core::ConstSpan<core::Id> elementIds);

    /// Makes a copy of the given elements in the form of a new document (see
    /// `copy()` for details), then deletes the elements and return the new
    /// document.
    ///
    /// Currently, this function performs a `hardDelete()` since this is the
    /// only deletion method implemented, but in the future, we are planning
    /// for this operation to use `softDelete()` by default.
    ///
    /// \sa `copy()`, `paste()`, `hardDelete()`.
    ///
    dom::DocumentPtr cut(core::ConstSpan<core::Id> elementIds);

    /// Returns a copy of the given elements in the form of a new document that
    /// can be used as argument to `paste()`.
    ///
    /// Note that the returned document does not necessarily conform to the
    /// same schema as `document()` and should typically not be manipulated
    /// other than for passing it as an argument to `paste()`.
    ///
    /// \sa `cut()`, `paste()`.
    ///
    dom::DocumentPtr copy(core::ConstSpan<core::Id> elementIds);

    /// Pastes the elements of the given `document` in this workspace's
    /// document.
    ///
    /// Returns a list with the top-level elements (i.e., not including their
    /// children) that have been pasted.
    ///
    /// \sa `cut()`, `copy()`.
    ///
    core::Array<core::Id> paste(dom::DocumentPtr document);

    /// Deletes the given elements and all incident elements, if any.
    ///
    void hardDelete(core::ConstSpan<core::Id> elementIds);

    /// Uncuts or deletes the given elements and all incident elements, if any.
    ///
    /// \sa `cut()`.
    ///
    void softDelete(core::ConstSpan<core::Id> elementIds);

    /// Returns the boundary of the given elements.
    ///
    /// See `vacomplex::boundary()` for details.
    ///
    /// \sa `outerBoundary()`, `star()`, `closure()`, `opening()`, `connected()`.
    ///
    core::Array<core::Id> boundary(core::ConstSpan<core::Id> elementIds) const;

    /// Returns the outer boundary of the given elements.
    ///
    /// This is equivalent to `boundary(closure(elementIds))`, but faster to
    /// compute.
    ///
    /// \sa `boundary()`, `star()`, `closure()`, `opening()`, `connected()`.
    ///
    core::Array<core::Id> outerBoundary(core::ConstSpan<core::Id> elementIds) const;

    /// Returns the union of the star of the given elements, excluding any cell
    /// that is already in `elementIds`.
    ///
    /// \sa `boundary()`, `outerBoundary()`, `closure()`, `opening()`, `connected()`.
    ///
    core::Array<core::Id> star(core::ConstSpan<core::Id> elementIds) const;

    /// Returns the union of the given elements and their boundary.
    ///
    /// \sa `boundary()`, `outerBoundary()`, `star()`, `opening()`, `connected()`.
    ///
    core::Array<core::Id> closure(core::ConstSpan<core::Id> elementIds) const;

    /// Returns the union of the given elements and their star.
    ///
    /// \sa `boundary()`, `outerBoundary()`, `star()`, `closure()`, `connected()`.
    ///
    core::Array<core::Id> opening(core::ConstSpan<core::Id> elementIds) const;

    /// Returns the union of the given elements and all other objects
    /// that are connected to them via boundary/star relationships.
    ///
    /// \sa `boundary()`, `outerBoundary()`, `star()`, `closure()`, `opening()`.
    ///
    core::Array<core::Id> connected(core::ConstSpan<core::Id> elementIds) const;

    /// Separates the given elements into connected components.
    ///
    core::Array<core::Array<core::Id>>
    connectedComponents(core::ConstSpan<core::Id> elementIds) const;

private:
    friend VacElement;

    // Factory methods to create a workspace element from a DOM tag name.
    //
    // An ElementCreator is a function taking a `Workspace*` as input and
    // returning an std::unique_ptr<Element>. This might be publicized later
    // for extensibility, but should then be adapted to allow interoperability
    // with Python.
    //
    using ElementCreator = std::unique_ptr<Element> (*)(Workspace*);

    static std::unordered_map<core::StringId, ElementCreator>& elementCreators_();

    static void
    registerElementClass_(core::StringId tagName, ElementCreator elementCreator);

    // This is the <vgc> element (the root).
    VacElement* rootVacElement_;
    std::unordered_map<core::Id, std::unique_ptr<Element>> elements_;
    std::unordered_map<core::Id, VacElement*> elementByVacInternalId_;
    core::Array<Element*> elementsWithError_;
    core::Array<Element*> elementsToUpdateFromDom_;
    void setPendingUpdateFromDom_(Element* element);
    void clearPendingUpdateFromDom_(Element* element);

    void removeElement_(Element* element);
    bool removeElement_(core::Id id); // returns if removal occured
    void clearElements_();

    void
    fillVacElementListsUsingTagName_(Element* root, detail::VacElementLists& ce) const;

    dom::DocumentSharedPtr document_;
    vacomplex::ComplexSharedPtr vac_;

    detail::ScopedUndoGroup createScopedUndoGroup_(core::StringId name) {
        return detail::createScopedUndoGroup(this, name);
    }

    void debugPrintWorkspaceTree_();

    // ---------------
    // VAC -> DOM Sync

    // Note: update from the VAC to the DOM happen after
    // each VAC operation.

    // Signal-slot connections

    void onVacCellSamplingQualityChanged_(const vacomplex::Cell* cell);
    VGC_SLOT(onVacCellSamplingQualityChanged_);

    void onVacNodesChanged_(const vacomplex::ComplexDiff& diff);
    VGC_SLOT(onVacNodesChanged, onVacNodesChanged_);

    // Helper methods

    void preUpdateDomFromVac_();
    void postUpdateDomFromVac_();
    void updateElementFromVac_( //
        VacElement* element,
        vacomplex::NodeModificationFlags flags);

    // Full rebuild

    void rebuildDomFromWorkspaceTree_();

    // ---------------
    // DOM -> VAC Sync

    // Note: update from the DOM to the VAC are deferred: they only happen
    // when document->emitPendingDiff() is called.

    bool isUpdatingVacFromDom_ = false;
    core::Id lastSyncedDomVersionId_ = {};

    // Signal-slot connections

    void onDocumentDiff_(const dom::Diff& diff);
    VGC_SLOT(onDocumentDiff, onDocumentDiff_);

    // Helper methods

    Int numDocumentDiffToSkip_ = 0;
    void flushDomDiff_();

    Element* createAppendElementFromDom_(dom::Element* domElement, Element* parent);

    void updateVacFromDom_(const dom::Diff& diff);
    void updateVacChildrenOrder_();

    // Full rebuild

    void rebuildWorkspaceTreeFromDom_();
    void rebuildVacFromWorkspaceTree_();
};

} // namespace vgc::workspace

#endif // VGC_WORKSPACE_WORKSPACE_H
