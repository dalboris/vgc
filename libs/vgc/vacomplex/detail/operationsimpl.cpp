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

#include <vgc/vacomplex/detail/operationsimpl.h>

#include <algorithm> // std::reverse
#include <unordered_set>

#include <vgc/core/algorithms.h> // sort
#include <vgc/core/array.h>
#include <vgc/geometry/intersect.h>
#include <vgc/vacomplex/algorithms.h>
#include <vgc/vacomplex/exceptions.h>
#include <vgc/vacomplex/inbetweenedge.h>
#include <vgc/vacomplex/inbetweenface.h>
#include <vgc/vacomplex/inbetweenvertex.h>
#include <vgc/vacomplex/keyedgedata.h>
#include <vgc/vacomplex/logcategories.h>

namespace vgc::vacomplex {

namespace detail {

Operations::Operations(Complex* complex)
    : complex_(complex) {

    // Ensure complex is non-null
    if (!complex) {
        throw LogicError("Cannot instantiate a VAC `Operations` with a null complex.");
    }

    if (++complex->numOperationsInProgress_ == 1) {
        // Increment version
        complex->version_ += 1;
    }
}

Operations::~Operations() {
    Complex* complex = this->complex();

    // TODO: try/catch?

    if (--complex->numOperationsInProgress_ == 0) {

        // Update geometry from boundary (for example, ensure that edges are
        // snapped to their end vertices). By iterating on cells by increasing
        // dimension, we avoid having to do this recursively.
        //
        core::Array<Cell*> cellsToUpdateGeometryFromBoundary;
        for (const ModifiedNodeInfo& info : complex->opDiff_.modifiedNodes_) {
            if (info.flags().has(NodeModificationFlag::BoundaryGeometryChanged)) {
                Cell* cell = info.node()->toCell();
                if (cell) {
                    cellsToUpdateGeometryFromBoundary.append(cell);
                }
            }
        }
        core::sort(cellsToUpdateGeometryFromBoundary, [](Cell* c1, Cell* c2) {
            return c1->cellType() < c2->cellType();
        });
        for (Cell* cell : cellsToUpdateGeometryFromBoundary) {
            if (cell->updateGeometryFromBoundary()) {
                onNodeModified_(cell, NodeModificationFlag::GeometryChanged);
            }
        }

        // Call finalizeConcat() for all new cells that may have been created
        // via a concatenation operation.
        //
        for (const CreatedNodeInfo& info : complex->opDiff_.createdNodes_) {
            Cell* cell = info.node()->toCell();
            if (cell) {
                switch (cell->cellType()) {
                case CellType::KeyEdge: {
                    KeyEdge* ke = cell->toKeyEdgeUnchecked();
                    ke->data().finalizeConcat();
                    break;
                }
                case CellType::KeyFace: {
                    KeyFace* kf = cell->toKeyFaceUnchecked();
                    kf->data().finalizeConcat();
                    break;
                }
                default:
                    break;
                }
            }
        }

        // Notify the outside world of the change.
        //
        complex->nodesChanged().emit(complex->opDiff_);

        // Clear diff data.
        //
        complex->opDiff_.clear();
        complex->temporaryCellSet_.clear();
    }
}

Group* Operations::createRootGroup() {
    Group* group = createNode_<Group>(complex());
    return group;
}

Group* Operations::createGroup(Group* parentGroup, Node* nextSibling) {
    Group* group = createNodeAt_<Group>(parentGroup, nextSibling, complex());
    return group;
}

KeyVertex* Operations::createKeyVertex(
    const geometry::Vec2d& position,
    Group* parentGroup,
    Node* nextSibling,
    core::AnimTime t) {

    KeyVertex* kv = createNodeAt_<KeyVertex>(parentGroup, nextSibling, t);

    // Topological attributes
    // -> None

    // Geometric attributes
    kv->position_ = position;

    return kv;
}

KeyEdge* Operations::createKeyOpenEdge(
    KeyVertex* startVertex,
    KeyVertex* endVertex,
    KeyEdgeData&& data,
    Group* parentGroup,
    Node* nextSibling) {

    KeyEdge* ke = createNodeAt_<KeyEdge>(parentGroup, nextSibling, startVertex->time());

    // Topological attributes
    ke->startVertex_ = startVertex;
    ke->endVertex_ = endVertex;
    addToBoundary_(ke, startVertex);
    addToBoundary_(ke, endVertex);

    // Geometric attributes
    ke->data().moveInit_(std::move(data));

    return ke;
}

KeyEdge* Operations::createKeyClosedEdge(
    KeyEdgeData&& data,
    Group* parentGroup,
    Node* nextSibling,
    core::AnimTime t) {

    KeyEdge* ke = createNodeAt_<KeyEdge>(parentGroup, nextSibling, t);

    // Topological attributes
    // -> None

    // Geometric attributes
    ke->data().moveInit_(std::move(data));

    return ke;
}

// Assumes `cycles` are valid.
// Assumes `nextSibling` is either `nullptr` or a child of `parentGroup`.
KeyFace* Operations::createKeyFace(
    core::Array<KeyCycle> cycles,
    Group* parentGroup,
    Node* nextSibling,
    core::AnimTime t) {

    KeyFace* kf = createNodeAt_<KeyFace>(parentGroup, nextSibling, t);

    // Topological attributes
    kf->cycles_ = std::move(cycles);
    for (const KeyCycle& cycle : kf->cycles()) {
        addToBoundary_(kf, cycle);
    }

    // Geometric attributes
    // -> None

    return kf;
}

// Assumes `kf` is not null.
// Assumes `cycle` is valid and matches kf's complex and time.
//
void Operations::addCycleToFace(KeyFace* kf, KeyCycle cycle) {
    // Topological attributes
    kf->cycles_.append(std::move(cycle));
    addToBoundary_(kf, kf->cycles_.last());
    // Geometric attributes
    // -> None
}

void Operations::hardDelete(core::ConstSpan<Node*> nodes, bool deleteIsolatedVertices) {

    deleteWithDependents_(nodes, deleteIsolatedVertices, false);
}

void Operations::hardDelete(Node* node, bool deleteIsolatedVertices) {

    deleteWithDependents_(std::array{node}, deleteIsolatedVertices, false);
}

namespace {

class ClassifiedCells {
public:
    ClassifiedCells() noexcept = default;

    ClassifiedCells(core::ConstSpan<Cell*> cells) {
        insert(cells.begin(), cells.end());
    }

    bool insert(Cell* cell) {
        return insert_(cell);
    }

    template<typename Iter>
    void insert(Iter first, Iter last) {
        while (first != last) {
            insert_(*first);
            ++first;
        }
    }

    void insert(const CellRangeView& rangeView) {
        for (Cell* cell : rangeView) {
            insert_(cell);
        }
    }

    void clear() {
        kvs_.clear();
        kes_.clear();
        kfs_.clear();
        ivs_.clear();
        ies_.clear();
        ifs_.clear();
    }

    core::Array<KeyVertex*>& kvs() {
        return kvs_;
    }

    const core::Array<KeyVertex*>& kvs() const {
        return kvs_;
    }

    core::Array<KeyEdge*>& kes() {
        return kes_;
    }

    const core::Array<KeyEdge*>& kes() const {
        return kes_;
    }

    core::Array<KeyFace*>& kfs() {
        return kfs_;
    }

    const core::Array<KeyFace*>& kfs() const {
        return kfs_;
    }

    core::Array<InbetweenVertex*>& ivs() {
        return ivs_;
    }

    const core::Array<InbetweenVertex*>& ivs() const {
        return ivs_;
    }

    core::Array<InbetweenEdge*>& ies() {
        return ies_;
    }

    const core::Array<InbetweenEdge*>& ies() const {
        return ies_;
    }

    core::Array<InbetweenFace*>& ifs() {
        return ifs_;
    }

    const core::Array<InbetweenFace*>& ifs() const {
        return ifs_;
    }

private:
    core::Array<KeyVertex*> kvs_;
    core::Array<KeyEdge*> kes_;
    core::Array<KeyFace*> kfs_;
    core::Array<InbetweenVertex*> ivs_;
    core::Array<InbetweenEdge*> ies_;
    core::Array<InbetweenFace*> ifs_;

    bool insert_(Cell* cell) {
        switch (cell->cellType()) {
        case vacomplex::CellType::KeyVertex: {
            KeyVertex* kv = cell->toKeyVertexUnchecked();
            if (!kvs_.contains(kv)) {
                kvs_.append(kv);
                return true;
            }
            break;
        }
        case vacomplex::CellType::KeyEdge: {
            KeyEdge* ke = cell->toKeyEdgeUnchecked();
            if (!kes_.contains(ke)) {
                kes_.append(ke);
                return true;
            }
            break;
        }
        case vacomplex::CellType::KeyFace: {
            KeyFace* kf = cell->toKeyFaceUnchecked();
            if (!kfs_.contains(kf)) {
                kfs_.append(kf);
                return true;
            }
            break;
        }
        case vacomplex::CellType::InbetweenVertex: {
            InbetweenVertex* iv = cell->toInbetweenVertexUnchecked();
            if (!ivs_.contains(iv)) {
                ivs_.append(iv);
                return true;
            }
            break;
        }
        case vacomplex::CellType::InbetweenEdge: {
            InbetweenEdge* ie = cell->toInbetweenEdgeUnchecked();
            if (!ies_.contains(ie)) {
                ies_.append(ie);
                return true;
            }
            break;
        }
        case vacomplex::CellType::InbetweenFace: {
            InbetweenFace* if_ = cell->toInbetweenFaceUnchecked();
            if (!ifs_.contains(if_)) {
                ifs_.append(if_);
                return true;
            }
            break;
        }
        }
        return false;
    }
};

class ResolvedSelection {
public:
    ResolvedSelection(core::ConstSpan<Node*> nodes) {
        for (Node* node : nodes) {
            if (node->isGroup()) {
                Group* group = node->toGroupUnchecked();
                visitGroup_(group);
            }
        }
        for (Node* node : nodes) {
            if (node->isCell()) {
                Cell* cell = node->toCellUnchecked();
                if (!cells_.contains(cell)) {
                    cells_.append(cell);
                    topCells_.append(cell);
                }
            }
        }
    }

    const core::Array<Group*>& groups() const {
        return groups_;
    }
    const core::Array<Cell*>& cells() const {
        return cells_;
    }

    const core::Array<Group*>& topGroups() const {
        return topGroups_;
    }
    const core::Array<Cell*>& topCells() const {
        return topCells_;
    }

private:
    core::Array<Group*> groups_;
    core::Array<Cell*> cells_;

    core::Array<Group*> topGroups_;
    core::Array<Cell*> topCells_;

    void visitChildNode_(Node* node) {
        if (node->isGroup()) {
            Group* group = node->toGroupUnchecked();
            visitGroup_(group);
        }
        else {
            Cell* cell = node->toCellUnchecked();
            if (cells_.contains(cell)) {
                topCells_.removeOne(cell);
            }
            else {
                cells_.append(cell);
            }
        }
    }

    void visitGroup_(Group* group) {
        if (groups_.contains(group)) {
            topGroups_.removeOne(group);
        }
        else {
            groups_.append(group);
            topGroups_.append(group);
            for (Node* child : *group) {
                visitChildNode_(child);
            }
        }
    }
};

} // namespace

// deleteIsolatedVertices is not supported yet
void Operations::softDelete(core::ConstSpan<Node*> nodes, bool deleteIsolatedVertices) {

    VGC_UNUSED(deleteIsolatedVertices);

    if (nodes.isEmpty()) {
        return;
    }

    constexpr bool smoothJoins = false;

    // classifyCells
    ClassifiedCells classifiedStar;
    auto classifyStar = [&](auto& cells) {
        classifiedStar.clear();
        for (const auto& cell : cells) {
            classifiedStar.insert(cell->star());
        }
    };

    // cells is updated to contain only cells that could not be uncut.
    auto uncutCells = [&](auto& cells) {
        using CellType = std::remove_pointer_t<
            typename core::RemoveCVRef<decltype(cells)>::value_type>;
        for (CellType*& cell : cells) {
            bool wasUncut = false;
            if constexpr (std::is_same_v<CellType, KeyVertex>) {
                wasUncut = uncutAtKeyVertex(cell, smoothJoins).success;
            }
            if constexpr (std::is_same_v<CellType, KeyEdge>) {
                wasUncut = uncutAtKeyEdge(cell).success;
            }
            if (wasUncut) {
                cell = nullptr;
            }
        }
        cells.removeAll(nullptr);
    };

    auto deleteCells = [this](auto&& cells) {
        // Note: deleteIsolatedVertices could remove cells that are
        // in `cells` and it would cause a crash.
        deleteWithDependents_(cells, false, true);
    };

    // Resolve selection
    ResolvedSelection selection(nodes);
    ClassifiedCells selectionCells(selection.cells());

    Complex* complex = nodes.first()->complex();
    complex->temporaryCellSet_ = closure(opening(selection.cells()));

    // Faces
    {
        core::Array<KeyFace*> kfs(selectionCells.kfs());
        if (!kfs.isEmpty()) {
            uncutCells(kfs);
        }
        deleteCells(kfs);
    }

    // Edges
    {
        core::Array<KeyEdge*> kes(selectionCells.kes());
        if (!kes.isEmpty()) {
            uncutCells(kes);
        }
        if (!kes.isEmpty()) {
            classifyStar(kes);
            core::Array<KeyFace*>& kfs = classifiedStar.kfs();
            uncutCells(kfs);
            uncutCells(kes);
        }
        deleteCells(kes);
    }

    // Vertices
    {
        core::Array<KeyVertex*> kvs(selectionCells.kvs());
        if (!kvs.isEmpty()) {
            uncutCells(kvs);
        }
        if (!kvs.isEmpty()) {
            classifyStar(kvs);
            core::Array<KeyEdge*>& kes = classifiedStar.kes();
            uncutCells(kes);
            uncutCells(kvs);
        }
        if (!kvs.isEmpty()) {
            classifyStar(kvs);
            core::Array<KeyFace*>& kfs = classifiedStar.kfs();
            uncutCells(kfs);
            core::Array<KeyEdge*>& kes = classifiedStar.kes();
            uncutCells(kes);
            uncutCells(kvs);
        }
        deleteCells(kvs);
    }

    // Groups
    for (Group* g : selection.topGroups()) {
        destroyChildlessNode_(g);
    }

    // Check closure for residual cells to remove such as isolated vertices.
    ClassifiedCells residualCells(complex->temporaryCellSet_);
    for (KeyVertex* kv : residualCells.kvs()) {
        if (kv->star().isEmpty()) {
            destroyChildlessNode_(kv);
        }
    }
}

core::Array<KeyCell*> Operations::simplify(
    core::Span<KeyVertex*> kvs,
    core::Span<KeyEdge*> kes,
    bool smoothJoins) {

    Complex* complex = nullptr;
    if (kvs.isEmpty()) {
        if (kes.isEmpty()) {
            return {};
        }
        complex = kes.first()->complex();
    }
    else {
        complex = kvs.first()->complex();
    }

    core::Array<KeyCell*> result;

    std::unordered_set<core::Id> resultEdgeIds;
    std::unordered_set<core::Id> resultFaceIds;

    for (KeyEdge* ke : kes) {
        UncutAtKeyEdgeResult res = uncutAtKeyEdge(ke);
        if (res.success) {
            if (res.removedKfId1) {
                resultFaceIds.erase(res.removedKfId1);
            }
            if (res.removedKfId2) {
                resultFaceIds.erase(res.removedKfId2);
            }
            if (res.resultKf) {
                resultFaceIds.insert(res.resultKf->id());
            }
        }
        else {
            // cannot uncut at edge: add it to the list of returned cells
            resultEdgeIds.insert(ke->id());
        }
    }

    for (KeyVertex* kv : kvs) {
        UncutAtKeyVertexResult res = uncutAtKeyVertex(kv, smoothJoins);
        if (res.success) {
            if (res.removedKeId1) {
                resultEdgeIds.erase(res.removedKeId1);
            }
            if (res.removedKeId2) {
                resultEdgeIds.erase(res.removedKeId2);
            }
            if (res.resultKe) {
                resultEdgeIds.insert(res.resultKe->id());
            }
            if (res.resultKf) {
                resultFaceIds.insert(res.resultKe->id());
            }
        }
        else {
            // cannot uncut at vertex: add it to the list of returned cells
            result.append(kv);
        }
    }

    for (core::Id id : resultEdgeIds) {
        Cell* cell = complex->findCell(id);
        if (cell) {
            KeyEdge* ke = cell->toKeyEdge();
            if (ke) {
                result.append(ke);
            }
        }
    }

    return result;
}

KeyVertex*
Operations::glueKeyVertices(core::Span<KeyVertex*> kvs, const geometry::Vec2d& position) {
    if (kvs.isEmpty()) {
        return nullptr;
    }
    KeyVertex* kv0 = kvs[0];

    bool hasDifferentKvs = false;
    for (KeyVertex* kv : kvs.subspan(1)) {
        if (kv != kv0) {
            hasDifferentKvs = true;
            break;
        }
    }

    if (!hasDifferentKvs) {
        setKeyVertexPosition(kv0, position);
        return kv0;
    }

    // Location: top-most input vertex
    Int n = kvs.length();
    core::Array<Node*> nodes(n);
    for (Int i = 0; i < n; ++i) {
        nodes[i] = kvs[i];
    }
    Node* topMostVertex = findTopMost(nodes);
    Group* parentGroup = topMostVertex->parentGroup();
    Node* nextSibling = topMostVertex->nextSibling();

    KeyVertex* newKv = createKeyVertex(position, parentGroup, nextSibling, kv0->time());

    std::unordered_set<KeyVertex*> seen;
    for (KeyVertex* kv : kvs) {
        bool inserted = seen.insert(kv).second;
        if (inserted) {
            substituteVertex_(kv, newKv);
            hardDelete(kv);
        }
    }

    return newKv;
}

namespace {

// Assumes `!samples.isEmpty()` and `numSamples >= 1`.
core::Array<geometry::Vec2d> computeApproximateUniformSamplingPositions(
    const geometry::StrokeSample2dArray& samples,
    Int numSamples) {

    core::Array<geometry::Vec2d> result;
    result.reserve(numSamples);
    result.append(samples.first().position());
    double l = samples.last().s();
    if (l > 0) {
        double deltaS = l / (numSamples - 1);
        double targetS = deltaS;
        const geometry::StrokeSample2d* s0 = &samples[0];
        for (const geometry::StrokeSample2d& s1 : samples) {
            double ds = s1.s() - s0->s();
            if (ds > 0 && targetS <= s1.s()) {
                double t = (targetS - s0->s()) / ds;
                result.append(core::fastLerp(s0->position(), s1.position(), t));
                targetS += deltaS;
            }
            s0 = &s1;
        }
    }
    while (result.length() < numSamples) {
        result.append(samples.last().position());
    }

    return result;
}

} // namespace

KeyEdge* Operations::glueKeyOpenEdges(core::ConstSpan<KeyHalfedge> khs) {
    return glueKeyOpenEdges_(khs);
}

KeyEdge* Operations::glueKeyOpenEdges(core::ConstSpan<KeyEdge*> kes) {

    Int n = kes.length();

    if (n == 0) {
        return nullptr;
    }
    else if (n == 1) {
        return kes[0];
    }

    // Detect which edge direction should be used for gluing.

    // Here, we handle the simple case where there are two edges that
    // already share at least one vertex.
    if (n == 2) {
        KeyEdge* ke0 = kes[0];
        KeyEdge* ke1 = kes[1];
        KeyVertex* ke00 = ke0->startVertex();
        KeyVertex* ke01 = ke0->endVertex();
        KeyVertex* ke10 = ke1->startVertex();
        KeyVertex* ke11 = ke1->endVertex();
        bool isAnyLoop = (ke00 == ke01) || (ke10 == ke11);
        bool isBestDirectionKnown = false;
        bool direction1 = true;
        if (!isAnyLoop) {
            bool shared00 = ke00 == ke10;
            bool shared11 = ke01 == ke11;
            bool shared01 = ke00 == ke11;
            bool shared10 = ke01 == ke10;
            if (shared00 != shared11) {
                // If the two edges have the same start vertex or the same
                // end vertex, we glue them in their intrinsic direction.
                direction1 = true;
                isBestDirectionKnown = true;
            }
            else if (shared01 != shared10) {
                // If the start (resp. end) vertex of ke0 is equal to the
                // end (resp. start) vertex of ke1, we want to glue them in reverse.
                direction1 = false;
                isBestDirectionKnown = true;
            }
        }
        if (isBestDirectionKnown) {
            std::array<KeyHalfedge, 2> khs = {
                KeyHalfedge(ke0, true), KeyHalfedge(ke1, direction1)};
            return glueKeyOpenEdges_(khs);
        }
    }

    constexpr Int numSamples = 10;

    core::Array<core::Array<geometry::Vec2d>> sampleArrays;
    sampleArrays.reserve(n);
    for (KeyEdge* ke : kes) {
        const geometry::StrokeSample2dArray& strokeSamples =
            ke->strokeSampling().samples();
        sampleArrays.append(
            computeApproximateUniformSamplingPositions(strokeSamples, numSamples));
    }

    core::Array<bool> bestDirections;
    core::Array<bool> tmpDirections(n);
    double bestCost = core::DoubleInfinity;

    for (Int i = 0; i < n; ++i) {
        double tmpCost = 0;
        const core::Array<geometry::Vec2d>& s0 = sampleArrays[i];
        tmpDirections[i] = true;
        for (Int j = 0; j < n; ++j) {
            if (j == i) {
                continue;
            }
            const core::Array<geometry::Vec2d>& s1 = sampleArrays[j];

            // costs per direction of edge j
            double costEj = 0;
            double costEjR = 0;

            for (Int iSample = 0; iSample < numSamples; ++iSample) {
                Int iSampleR = numSamples - 1 - iSample;
                const geometry::Vec2d& s0i = s0.getUnchecked(iSample);
                costEj += (s0i - s1.getUnchecked(iSample)).squaredLength();
                costEjR += (s0i - s1.getUnchecked(iSampleR)).squaredLength();
            }

            if (costEj <= costEjR) {
                tmpDirections[j] = true;
                tmpCost += costEj;
            }
            else {
                tmpDirections[j] = false;
                tmpCost += costEjR;
            }
        }
        if (tmpCost < bestCost) {
            bestDirections = tmpDirections;
            bestCost = tmpCost;
        }
    }

    core::Array<KeyHalfedge> khs;
    khs.reserve(n);
    for (Int i = 0; i < n; ++i) {
        khs.emplaceLast(kes[i], bestDirections[i]);
    }

    return glueKeyOpenEdges_(khs);
}

KeyEdge* Operations::glueKeyClosedEdges(core::ConstSpan<KeyHalfedge> khs) {
    Int n = khs.length();

    if (n == 0) {
        return nullptr;
    }
    else if (n == 1) {
        return khs[0].edge();
    }

    constexpr Int numCostSamples = 10;
    constexpr Int costSampleStride = 10;
    constexpr Int numSamples = numCostSamples * costSampleStride;

    core::Array<core::Array<geometry::Vec2d>> sampleArrays;
    core::Array<double> arclengths;
    sampleArrays.reserve(n);
    arclengths.reserve(n);
    for (const KeyHalfedge& kh : khs) {
        KeyEdge* ke = kh.edge();
        const geometry::StrokeSample2dArray& strokeSamples =
            ke->strokeSampling().samples();
        sampleArrays.append(
            computeApproximateUniformSamplingPositions(strokeSamples, numSamples + 1));
        core::Array<geometry::Vec2d>& samples = sampleArrays.last();
        if (!kh.direction()) {
            std::reverse(samples.begin(), samples.end());
        }
        // since it is closed, first and last are the same
        samples.removeLast();
        arclengths.append(strokeSamples.last().s());
    }

    core::Array<double> bestUOffsets;
    core::Array<double> tmpUOffsets(n);

    double bestCost = core::DoubleInfinity;
    double deltaU = 1. / numSamples;

    for (Int i = 0; i < n; ++i) {
        double tmpCost = 0;
        const core::Array<geometry::Vec2d>& s0 = sampleArrays[i];
        tmpUOffsets[i] = 0.;
        for (Int j = 0; j < n; ++j) {
            if (j == i) {
                continue;
            }
            const core::Array<geometry::Vec2d>& s1 = sampleArrays[j];

            // best cost for shifts of halfedge j
            double bestCostHj = core::DoubleInfinity;

            for (Int k = 0; k < numSamples; ++k) {
                // cost for halfedge j with shift k
                double costHjk = 0;
                for (Int iCostSample = 0; iCostSample < numCostSamples; ++iCostSample) {
                    // the shift must be reversed for use with AbstractStroke2d
                    Int iSample = (iCostSample * costSampleStride + k) % numSamples;
                    const geometry::Vec2d& s0i = s0.getUnchecked(iSample);
                    const geometry::Vec2d& s1i = s1.getUnchecked(iSample);
                    costHjk += (s0i - s1i).squaredLength();
                }
                if (costHjk < bestCostHj) {
                    tmpUOffsets[j] = deltaU * k;
                    bestCostHj = costHjk;
                }
            }

            tmpCost += bestCostHj;
        }
        if (tmpCost < bestCost) {
            bestUOffsets = tmpUOffsets;
            bestCost = tmpCost;
        }
    }

    return glueKeyClosedEdges_(khs, bestUOffsets);
}

KeyEdge* Operations::glueKeyClosedEdges(core::ConstSpan<KeyEdge*> kes) {
    Int n = kes.length();

    if (n == 0) {
        return nullptr;
    }
    else if (n == 1) {
        return kes[0];
    }

    constexpr Int numCostSamples = 10;
    constexpr Int costSampleStride = 10;
    constexpr Int numSamples = numCostSamples * costSampleStride;

    core::Array<core::Array<geometry::Vec2d>> sampleArrays;
    core::Array<double> arclengths;
    sampleArrays.reserve(n);
    arclengths.reserve(n);
    for (KeyEdge* ke : kes) {
        const geometry::StrokeSample2dArray& strokeSamples =
            ke->strokeSampling().samples();
        sampleArrays.append(
            computeApproximateUniformSamplingPositions(strokeSamples, numSamples + 1));
        // since it is closed, first and last are the same
        sampleArrays.last().removeLast();
        arclengths.append(strokeSamples.last().s());
    }

    core::Array<bool> bestDirections;
    core::Array<bool> tmpDirections(n);
    core::Array<double> bestUOffsets;
    core::Array<double> tmpUOffsets(n);

    double bestCost = core::DoubleInfinity;
    double deltaU = 1. / numSamples;

    for (Int i = 0; i < n; ++i) {
        double tmpCost = 0;
        const core::Array<geometry::Vec2d>& s0 = sampleArrays[i];
        tmpDirections[i] = true;
        tmpUOffsets[i] = 0.;
        for (Int j = 0; j < n; ++j) {
            if (j == i) {
                continue;
            }
            const core::Array<geometry::Vec2d>& s1 = sampleArrays[j];

            // best cost for (direction, shift) of edge j
            double bestCostEj = core::DoubleInfinity;

            for (Int k = 0; k < numSamples; ++k) {
                // costs per direction of edge j with shift k
                double costEjk = 0;
                double costEjRk = 0;
                for (Int iCostSample = 0; iCostSample < numCostSamples; ++iCostSample) {
                    // the shift must be reversed for use with AbstractStroke2d
                    Int iSample = iCostSample * costSampleStride;
                    Int jSample = (iSample + k) % numSamples;
                    Int jSampleR = numSamples - 1 - jSample;
                    const geometry::Vec2d& s0i = s0.getUnchecked(iSample);
                    costEjk += (s0i - s1.getUnchecked(jSample)).squaredLength();
                    costEjRk += (s0i - s1.getUnchecked(jSampleR)).squaredLength();
                }
                if (costEjk < bestCostEj) {
                    tmpUOffsets[j] = deltaU * k;
                    tmpDirections[j] = true;
                    bestCostEj = costEjk;
                }
                if (costEjRk < bestCostEj) {
                    tmpUOffsets[j] = deltaU * k;
                    tmpDirections[j] = false;
                    bestCostEj = costEjRk;
                }
            }

            tmpCost += bestCostEj;
        }
        if (tmpCost < bestCost) {
            bestDirections = tmpDirections;
            bestUOffsets = tmpUOffsets;
            bestCost = tmpCost;
        }
    }

    core::Array<KeyHalfedge> khs;
    khs.reserve(n);
    for (Int i = 0; i < n; ++i) {
        khs.emplaceLast(kes[i], bestDirections[i]);
    }

    return glueKeyClosedEdges_(khs, bestUOffsets);
}

core::Array<KeyEdge*> Operations::unglueKeyEdges(KeyEdge* targetKe) {
    core::Array<KeyEdge*> result;
    if (countUses_(targetKe) <= 1) {
        result.append(targetKe);
        return result;
    }

    // TODO: handle temporal star.

    // Helper
    auto duplicateTargetKe = [this, targetKe, &result]() {
        KeyEdge* newKe = nullptr;
        KeyEdgeData dataDuplicate = targetKe->data();
        if (targetKe->isClosed()) {
            newKe = createKeyClosedEdge(
                std::move(dataDuplicate),
                targetKe->parentGroup(),
                targetKe->nextSibling());
        }
        else {
            newKe = createKeyOpenEdge(
                targetKe->startVertex(),
                targetKe->endVertex(),
                std::move(dataDuplicate),
                targetKe->parentGroup(),
                targetKe->nextSibling());
        }
        result.append(newKe);
        return newKe;
    };

    // Substitute targetKe by a duplicate in each of its use.
    // Note: star is copied for safety since it may be modified in loop.
    for (Cell* cell : targetKe->star().copy()) {
        switch (cell->cellType()) {
        case CellType::KeyFace: {
            KeyFace* kf = cell->toKeyFaceUnchecked();
            for (KeyCycle& cycle : kf->cycles_) {
                if (cycle.steinerVertex_) {
                    continue;
                }
                KeyHalfedge first = cycle.halfedges().first();
                if (!first.isClosed()) {
                    for (KeyHalfedge& kheRef : cycle.halfedges_) {
                        if (kheRef.edge() == targetKe) {
                            KeyEdge* newKe = duplicateTargetKe();
                            kheRef = KeyHalfedge(newKe, kheRef.direction());
                            addToBoundary_(kf, newKe);
                        }
                    }
                    VGC_ASSERT(cycle.isValid());
                }
                else if (first.edge() == targetKe) {
                    KeyEdge* newKe = duplicateTargetKe();
                    for (KeyHalfedge& kheRef : cycle.halfedges_) {
                        kheRef = KeyHalfedge(newKe, kheRef.direction());
                    }
                    addToBoundary_(kf, newKe);
                    // TODO: instead of having a copy of the edge used N times
                    // use a single edge with its geometry looped N times.
                    // See Boris Dalstein's thesis page 187.
                    VGC_ASSERT(cycle.isValid());
                }
            }
            removeFromBoundary_(kf, targetKe);
            break;
        }
        default:
            throw core::LogicError(
                "unglueKeyEdges() doesn't support temporal cells in edge star.");
        }
    }

    // Delete targetKe
    hardDelete(targetKe);

    return result;
}

core::Array<KeyVertex*> Operations::unglueKeyVertices(
    KeyVertex* targetKv,
    core::Array<std::pair<core::Id, core::Array<KeyEdge*>>>& ungluedKeyEdges) {

    core::Array<KeyVertex*> result;
    if (countUses_(targetKv) <= 1) {
        result.append(targetKv);
        return result;
    }

    // TODO: handle temporal star.

    // Unglue incident key edges.
    for (Cell* cell : targetKv->star().copy()) {
        switch (cell->cellType()) {
        case CellType::KeyEdge: {
            KeyEdge* ke = cell->toKeyEdgeUnchecked();
            core::Id id = ke->id();
            core::Array<KeyEdge*> a = unglueKeyEdges(ke);
            if (a.length() > 1) {
                ungluedKeyEdges.emplaceLast(id, std::move(a));
            }
            break;
        }
        default:
            break;
        }
    }

    // Helper
    auto duplicateTargetKv = [this, targetKv, &result]() {
        KeyVertex* newKv = createKeyVertex(
            targetKv->position(),
            targetKv->parentGroup(),
            targetKv->nextSibling(),
            targetKv->time());
        result.append(newKv);
        return newKv;
    };

    // Helper. Assumes the replaced key vertex is `targetKv`.
    auto substituteTargetKvAtStartOrEndOfKhe = //
        [this, targetKv](const KeyHalfedge& khe, bool startVertex, KeyVertex* newKv) {
            //
            KeyEdge* ke = khe.edge();

            KeyVertex* otherEndKv = nullptr;
            if (khe.direction() == startVertex) {
                otherEndKv = ke->endVertex();
                ke->startVertex_ = newKv;
            }
            else {
                otherEndKv = ke->startVertex();
                ke->endVertex_ = newKv;
            }

            if (otherEndKv != targetKv) {
                removeFromBoundary_(ke, targetKv);
            }

            addToBoundary_(ke, newKv);
        };

    // Substitute targetKv by a duplicate in each of its use.
    // Note: star is copied for safety since it may be modified in loop.
    for (Cell* cell : targetKv->star().copy()) {
        switch (cell->cellType()) {
        case CellType::KeyEdge: {
            KeyEdge* ke = cell->toKeyEdgeUnchecked();
            bool hasFaceInStar = false;
            for (Cell* keStarCell : ke->star()) {
                if (keStarCell->cellType() == CellType::KeyFace) {
                    hasFaceInStar = true;
                    break;
                }
            }
            if (!hasFaceInStar) {
                if (ke->isStartVertex(targetKv)) {
                    KeyVertex* newKv = duplicateTargetKv();
                    ke->startVertex_ = newKv;
                    addToBoundary_(ke, newKv);
                }
                if (ke->isEndVertex(targetKv)) {
                    KeyVertex* newKv = duplicateTargetKv();
                    ke->endVertex_ = newKv;
                    addToBoundary_(ke, newKv);
                }
                removeFromBoundary_(ke, targetKv);
            }
            break;
        }
        case CellType::KeyFace: {
            KeyFace* kf = cell->toKeyFaceUnchecked();
            for (KeyCycle& cycle : kf->cycles_) {
                if (cycle.steinerVertex()) {
                    if (cycle.steinerVertex() == targetKv) {
                        KeyVertex* newKv = duplicateTargetKv();
                        cycle.steinerVertex_ = newKv;
                        addToBoundary_(kf, newKv);
                    }
                    continue;
                }
                Int numHalfedges = cycle.halfedges_.length();
                // substitute at face corner uses
                for (Int i = 0; i < numHalfedges; ++i) {
                    KeyHalfedge& khe1 = cycle.halfedges_[i];
                    if (khe1.startVertex() == targetKv) {
                        Int previousKheIndex = (i - 1 + numHalfedges) % numHalfedges;
                        KeyHalfedge& khe0 = cycle.halfedges_[previousKheIndex];

                        // (?)---khe0-->(targetKv)---khe1-->(?)
                        KeyVertex* newKv = duplicateTargetKv();
                        substituteTargetKvAtStartOrEndOfKhe(khe0, false, newKv);
                        substituteTargetKvAtStartOrEndOfKhe(khe1, true, newKv);
                        // (?)---khe0-->( newKv  )---khe1-->(?)

                        addToBoundary_(kf, newKv);
                    }
                }
                VGC_ASSERT(cycle.isValid());
            }
            removeFromBoundary_(kf, targetKv);
            break;
        }
        default:
            throw core::LogicError(
                "unglueKeyVertices() doesn't support temporal cells in edge star.");
        }
    }

    // Delete targetKv
    hardDelete(targetKv);

    return result;
}

CutEdgeResult
Operations::cutEdge(KeyEdge* ke, const geometry::CurveParameter& parameter) {

    const geometry::AbstractStroke2d* oldStroke = ke->data().stroke();

    if (ke->isClosed()) {
        KeyEdgeData newKeData =
            KeyEdgeData::fromSlice(ke->data(), parameter, parameter, 1);

        geometry::Vec2d vertexPos = newKeData.stroke()->endPositions()[0];

        KeyVertex* newKv =
            createKeyVertex(vertexPos, ke->parentGroup(), ke->nextSibling(), ke->time());

        KeyEdge* newKe =
            createKeyOpenEdge(newKv, newKv, std::move(newKeData), ke->parentGroup(), ke);

        // Substitute all usages of old edge by new edge
        KeyHalfedge oldKhe(ke, true);
        KeyHalfedge newKhe(newKe, true);
        substituteEdge_(oldKhe, newKhe);

        // Since substitute expects end vertices to be the same,
        // it didn't add our targetKv to its star's boundary.
        // So we do it manually here.
        for (Cell* cell : newKe->star()) {
            addToBoundary_(cell, newKv);
        }

        // Delete old edge
        hardDelete(ke);

        return CutEdgeResult(newKe, newKv, newKe);
    }
    else {
        KeyEdgeData newKeData1 = KeyEdgeData::fromSlice(
            ke->data(), geometry::CurveParameter(0, 0), parameter, 0);

        KeyEdgeData newKeData2 = KeyEdgeData::fromSlice(
            ke->data(),
            parameter,
            geometry::CurveParameter(oldStroke->numSegments() - 1, 1),
            0);

        geometry::Vec2d vertexPos = newKeData2.stroke()->endPositions()[0];

        KeyVertex* newKv =
            createKeyVertex(vertexPos, ke->parentGroup(), ke->nextSibling(), ke->time());

        KeyEdge* newKe1 = createKeyOpenEdge(
            ke->startVertex(), newKv, std::move(newKeData1), ke->parentGroup(), ke);
        KeyEdge* newKe2 = createKeyOpenEdge(
            newKv, ke->endVertex(), std::move(newKeData2), ke->parentGroup(), ke);

        // Substitute all usages of ke by (newKe1, newKe2) in incident faces.
        //
        for (Cell* starCell : ke->star().copy()) {
            KeyFace* kf = starCell->toKeyFace();
            if (!kf) {
                continue;
            }
            for (KeyCycle& cycle : kf->cycles_) {
                if (cycle.steinerVertex()) {
                    continue;
                }
                core::Array<KeyHalfedge>& cycleKhes = cycle.halfedges_;
                for (auto it = cycleKhes.begin(); it != cycleKhes.end(); ++it) {
                    KeyHalfedge& khe = *it;
                    if (khe.edge() == ke) {
                        if (khe.direction()) {
                            khe.setEdge(newKe1);
                            it = cycleKhes.emplace(it + 1, newKe2, true);
                        }
                        else {
                            khe.setEdge(newKe2);
                            it = cycleKhes.emplace(it + 1, newKe1, false);
                        }
                    }
                }
                VGC_ASSERT(cycle.isValid());
            }

            removeFromBoundary_(kf, ke);
            addToBoundary_(kf, newKe1);
            addToBoundary_(kf, newKe2);
            addToBoundary_(kf, newKv);
        }

        // Delete old edge
        hardDelete(ke);

        return CutEdgeResult(newKe1, newKv, newKe2);
    }
}

void Operations::cutGlueFaceWithVertex(KeyFace* kf, KeyVertex* kv) {
    kf->cycles_.append(KeyCycle(kv));
    addToBoundary_(kf, kv);
}

KeyVertex* Operations::cutFaceWithVertex(KeyFace* kf, const geometry::Vec2d& position) {
    KeyVertex* newKv =
        createKeyVertex(position, kf->parentGroup(), kf->nextSibling(), kf->time());
    cutGlueFaceWithVertex(kf, newKv);
    return newKv;
}

CutFaceResult Operations::cutGlueFace(
    KeyFace* kf,
    const KeyEdge* ke,
    OneCycleCutPolicy oneCycleCutPolicy,
    TwoCycleCutPolicy twoCycleCutPolicy) {

    if (!ke->isClosed()) {
        KeyVertex* startVertex = ke->startVertex();
        KeyVertex* endVertex = ke->endVertex();

        core::Array<KeyFaceVertexUsageIndex> startIndexCandidates;
        core::Array<KeyFaceVertexUsageIndex> endIndexCandidates;

        Int i = 0;
        for (const auto& cycle : kf->cycles()) {
            KeyVertex* sv = cycle.steinerVertex();
            if (sv) {
                if (sv == startVertex) {
                    startIndexCandidates.emplaceLast(i, 0);
                }
                if (sv == endVertex) {
                    endIndexCandidates.emplaceLast(i, 0);
                }
            }
            else {
                Int j = 0;
                for (const auto& h : cycle.halfedges()) {
                    KeyVertex* kv = h.startVertex();
                    if (kv == startVertex) {
                        startIndexCandidates.emplaceLast(i, j);
                    }
                    if (kv == endVertex) {
                        endIndexCandidates.emplaceLast(i, j);
                    }
                    ++j;
                }
            }
            ++i;
        }

        // TODO: find best usages
        // XXX: based on policies ?
        KeyFaceVertexUsageIndex startIndex = startIndexCandidates.first();
        KeyFaceVertexUsageIndex endIndex = endIndexCandidates.first();

        KeyHalfedge khe(const_cast<KeyEdge*>(ke), true);

        if (startVertex == endVertex) {
            // TODO: choose halfedge direction based on twoCycleCutPolicy.
        }

        return cutGlueFace(
            kf, khe, startIndex, endIndex, oneCycleCutPolicy, twoCycleCutPolicy);
    }
    else {
        KeyHalfedge khe(const_cast<KeyEdge*>(ke), true);

        // Do cut-glue with closed edge.

        if (oneCycleCutPolicy == OneCycleCutPolicy::Auto) {
            // TODO: find best policy.
            oneCycleCutPolicy = OneCycleCutPolicy::Disk;
        }

        switch (oneCycleCutPolicy) {
        case OneCycleCutPolicy::Auto:
        case OneCycleCutPolicy::Disk: {
            // Let's assume our edge is in the interior of the face.
            core::Array<KeyCycle> cycles1;
            core::Array<KeyCycle> cycles2;

            KeyCycle newCycle({khe});

            // TODO: use KeyFace winding rule.
            geometry::WindingRule windingRule = geometry::WindingRule::Odd;
            constexpr Int numSamplesPerContenanceTest = 20;
            constexpr double ratioThreshold = 0.5;

            cycles1.append(newCycle);
            cycles2.append(newCycle.reversed());

            for (const auto& cycle : kf->cycles()) {
                if (newCycle.interiorContainedRatio(
                        cycle, windingRule, numSamplesPerContenanceTest)
                    > ratioThreshold) {
                    cycles1.append(cycle);
                }
                else {
                    cycles2.append(cycle);
                }
            }

            // Create the faces
            KeyFace* kf1 =
                createKeyFace(std::move(cycles1), kf->parentGroup(), kf, kf->time());
            kf1->data().setProperties(kf->data().properties());
            KeyFace* kf2 =
                createKeyFace(std::move(cycles2), kf->parentGroup(), kf, kf->time());
            kf2->data().setProperties(kf->data().properties());

            // TODO: substitute in inbetween faces.

            // Delete original face.
            hardDelete(kf);

            return CutFaceResult(kf1, khe.edge(), kf2);
        }
        case OneCycleCutPolicy::Mobius: {
            KeyCycle newCycle({khe, khe});
            addCycleToFace(kf, std::move(newCycle));
            return CutFaceResult(kf, khe.edge(), kf);
        }
        case OneCycleCutPolicy::Torus: {
            KeyCycle newCycle({khe});
            addCycleToFace(kf, newCycle);
            addCycleToFace(kf, newCycle.reversed());
            return CutFaceResult(kf, khe.edge(), kf);
        }
        }

        throw LogicError("cutGlueFace: invalid oneCycleCutPolicy.");
    }
}

namespace {

KeyPath rotatedCycleHalfedges_(const KeyCycle& cycle, Int newStart) {
    core::Array<KeyHalfedge> result;
    const auto& khes = cycle.halfedges();
    result.reserve(khes.length());
    result.extend(khes.begin() + newStart, khes.end());
    result.extend(khes.begin(), khes.begin() + newStart);
    return KeyPath(std::move(result));
};

} // namespace

CutFaceResult Operations::cutGlueFace(
    KeyFace* kf,
    const KeyHalfedge& khe,
    KeyFaceVertexUsageIndex startIndex,
    KeyFaceVertexUsageIndex endIndex,
    OneCycleCutPolicy oneCycleCutPolicy,
    TwoCycleCutPolicy twoCycleCutPolicy) {

    Int cycleIndex1 = startIndex.cycleIndex();
    Int cycleIndex2 = endIndex.cycleIndex();

    if (cycleIndex1 == cycleIndex2) {
        // One-cycle cut case

        KeyCycle& cycle = kf->cycles_[cycleIndex1];

        // If one path must be empty, it will be path2.
        KeyPath path1 =
            subPath_(cycle, endIndex.componentIndex(), startIndex.componentIndex(), true);
        KeyPath path2 =
            subPath_(cycle, startIndex.componentIndex(), endIndex.componentIndex());

        if (oneCycleCutPolicy == OneCycleCutPolicy::Auto) {

            // Default fallback policy.
            oneCycleCutPolicy = OneCycleCutPolicy::Disk;

            // XXX: improve method of identifying best policy.
            //      identify punctured torus case ?
            if (!path1.halfedges().isEmpty() && !path2.halfedges().isEmpty()) {
                geometry::Vec2dArray poly1 = path1.sampleCenterline();
                geometry::Vec2dArray poly2 = path2.sampleCenterline();

                Int count = 0;
                for (Int i = 1; i + 2 < poly1.length(); ++i) {
                    for (Int j = 1; j + 2 < poly2.length(); ++j) {
                        if (geometry::fastSemiOpenSegmentIntersects(
                                poly1[i], poly1[i + 1], poly2[j], poly2[j + 1])) {
                            ++count;
                        }
                    }
                }

                if (count % 2 == 1) {
                    oneCycleCutPolicy = OneCycleCutPolicy::Mobius;
                }
            }
        }

        VGC_ASSERT(oneCycleCutPolicy != OneCycleCutPolicy::Auto);

        switch (oneCycleCutPolicy) {
        case OneCycleCutPolicy::Auto:
        case OneCycleCutPolicy::Disk: {
            core::Array<KeyCycle> cycles1;
            core::Array<KeyCycle> cycles2;

            KeyCycle newCycle1;
            {
                KeyPath path = path1;
                path.append(khe);
                newCycle1 = KeyCycle(std::move(path));
            }
            VGC_ASSERT(newCycle1.isValid());

            KeyCycle newCycle2;
            {
                KeyPath path = path2;
                path.append(khe.opposite());
                newCycle2 = KeyCycle(std::move(path));
            }
            VGC_ASSERT(newCycle2.isValid());

            cycles1.append(newCycle1);
            cycles2.append(newCycle2);

            // TODO: use KeyFace winding rule.
            geometry::WindingRule windingRule = geometry::WindingRule::Odd;
            constexpr Int numSamplesPerContenanceTest = 20;

            Int cycleIndex = -1;
            for (const auto& otherCycle : kf->cycles()) {
                ++cycleIndex;
                if (cycleIndex == cycleIndex1) {
                    continue;
                }
                double r1 = newCycle1.interiorContainedRatio(
                    otherCycle, windingRule, numSamplesPerContenanceTest);
                double r2 = newCycle2.interiorContainedRatio(
                    otherCycle, windingRule, numSamplesPerContenanceTest);
                if (r1 >= r2) {
                    cycles1.append(otherCycle);
                }
                else {
                    cycles2.append(otherCycle);
                }
            }

            // Create the faces
            KeyFace* kf1 =
                createKeyFace(std::move(cycles1), kf->parentGroup(), kf, kf->time());
            kf1->data().setProperties(kf->data().properties());
            KeyFace* kf2 =
                createKeyFace(std::move(cycles2), kf->parentGroup(), kf, kf->time());
            kf2->data().setProperties(kf->data().properties());

            // TODO: substitute in inbetween faces.

            // Delete original face.
            hardDelete(kf);

            return CutFaceResult(kf1, khe.edge(), kf2);
        }
        case OneCycleCutPolicy::Mobius: {
            KeyCycle newCycle;
            {
                KeyPath path = path1;
                path.append(khe);
                path.extendReversed(path2);
                path.append(khe);
                newCycle = KeyCycle(std::move(path));
            }
            VGC_ASSERT(newCycle.isValid());

            kf->cycles_[cycleIndex1] = std::move(newCycle);
            addToBoundary_(kf, khe.edge());

            return CutFaceResult(kf, khe.edge(), kf);
        }
        case OneCycleCutPolicy::Torus: {
            KeyCycle newCycle1;
            {
                KeyPath path = path1;
                path.append(khe);
                newCycle1 = KeyCycle(std::move(path));
            }
            VGC_ASSERT(newCycle1.isValid());

            KeyCycle newCycle2;
            {
                KeyPath path = path2;
                path.append(khe.opposite());
                newCycle2 = KeyCycle(std::move(path));
            }
            VGC_ASSERT(newCycle2.isValid());

            kf->cycles_[cycleIndex1] = std::move(newCycle1);
            kf->cycles_.append(std::move(newCycle2));
            addToBoundary_(kf, khe.edge());

            return CutFaceResult(kf, khe.edge(), kf);
        }
        }

        throw LogicError("cutGlueFace: invalid oneCycleCutPolicy.");
    }
    else {
        // Two-cycle cut case

        const KeyCycle& cycle1 = kf->cycles_[cycleIndex1];
        const KeyCycle& cycle2 = kf->cycles_[cycleIndex2];

        KeyPath path1 = rotatedCycleHalfedges_(cycle1, startIndex.componentIndex());
        KeyPath path2 = rotatedCycleHalfedges_(cycle2, endIndex.componentIndex());

        if (twoCycleCutPolicy == TwoCycleCutPolicy::Auto) {

            // Default fallback policy.
            twoCycleCutPolicy = TwoCycleCutPolicy::ReverseNone;

            if (!path1.isSingleVertex() && !path2.isSingleVertex()) {
                // Around each end vertex, the plane can be seen as divided into sectors by
                // the incident halfedges. In each of these sectors we want the winding to
                // remain unchanged if possible.

                core::Array<const KeyCycle*> otherCycles;
                Int cycleIndex = -1;
                for (const auto& otherCycle : kf->cycles()) {
                    ++cycleIndex;
                    if (cycleIndex == cycleIndex1 || cycleIndex == cycleIndex2) {
                        continue;
                    }
                    if (otherCycle.steinerVertex()) {
                        continue;
                    }
                    otherCycles.append(&otherCycle);
                }

                // assumes h2 = h1.previous().opposite()
                auto toSectorPoint =
                    [kf](
                        const geometry::Vec2d& p,
                        const RingKeyHalfedge& rh1,
                        const RingKeyHalfedge& rh2) -> std::optional<geometry::Vec2d> {
                    double angle1 = rh1.angle();
                    double angle2 = rh2.angle();
                    if (rh2 < rh1) {
                        angle2 += 2 * core::pi;
                    }
                    else if (angle2 == angle1) {
                        return std::nullopt;
                    }
                    double angle = (angle1 + angle2) * 0.5;

                    geometry::Rect2d kfBbox = kf->boundingBox();
                    double delta = (std::max)(kfBbox.width(), kfBbox.height());
                    delta *= core::epsilon;

                    return p + geometry::Vec2d(std::cos(angle), std::sin(angle)) * delta;
                };

                struct WindingSample {
                    geometry::Vec2d position;
                    std::array<Int, 4> numbers;
                };

                core::Array<WindingSample> windingSamples;

                // TODO: use KeyFace winding rule.
                geometry::WindingRule windingRule = geometry::WindingRule::Odd;

                auto processRing = [&](KeyVertex* kv) {
                    geometry::Vec2d p = kv->position();
                    core::Array<RingKeyHalfedge> ring =
                        khe.startVertex()->ringHalfedges();

                    auto prevIt = ring.end() - 1;
                    for (auto it = ring.begin(); it != ring.end(); prevIt = it, ++it) {
                        if (std::optional<geometry::Vec2d> sp =
                                toSectorPoint(p, *prevIt, *it)) {

                            geometry::Vec2d spPos = sp.value();
                            Int number0 = 0;
                            for (const auto* otherCycle : otherCycles) {
                                number0 += otherCycle->computeWindingNumberAt(spPos);
                            }
                            Int number1 = cycle1.computeWindingNumberAt(spPos);
                            Int number2 = cycle2.computeWindingNumberAt(spPos);
                            WindingSample& sample = windingSamples.emplaceLast();
                            sample.position = spPos;
                            sample.numbers[0] = number0 + number1 + number2;
                            sample.numbers[1] = number0 - number1 + number2;
                            sample.numbers[2] = number0 + number1 - number2;
                            sample.numbers[3] = number0 - number1 - number2;
                        }
                    }
                };

                processRing(khe.startVertex());
                if (khe.endVertex() != khe.startVertex()) {
                    processRing(khe.endVertex());
                }

                switch (windingRule) {
                case geometry::WindingRule::Odd: {
                    // All combinaisons keep the appearance,
                    // but we prefer lower numbers and zeros.
                    using Sums = std::array<Int, 3>;
                    Sums zeroSums = {0, 0, 0};
                    std::array<Sums, 4> sumsPerPolicy = {
                        zeroSums, zeroSums, zeroSums, zeroSums};
                    for (const WindingSample& sample : windingSamples) {
                        Int number0 = sample.numbers[0];
                        for (Int i = 0; i < 4; ++i) {
                            Int number = sample.numbers[i];
                            Int absNumber = std::abs(number);
                            if (absNumber % 2 == 0) {
                                sumsPerPolicy[i][0] += std::abs(number);
                            }
                            else {
                                sumsPerPolicy[i][1] += std::abs(number);
                            }
                            if (number != 0) {
                                if (number0 == 0) {
                                    sumsPerPolicy[i][2] += 1;
                                }
                                else if (number * number0 < 0) {
                                    sumsPerPolicy[i][2] += 1;
                                }
                            }
                        }
                    }
                    Int minIndex = std::distance(
                        sumsPerPolicy.begin(),
                        std::min_element(sumsPerPolicy.begin(), sumsPerPolicy.end()));
                    switch (minIndex) {
                    case 0:
                        twoCycleCutPolicy = TwoCycleCutPolicy::ReverseNone;
                        break;
                    case 1:
                        twoCycleCutPolicy = TwoCycleCutPolicy::ReverseStart;
                        break;
                    case 2:
                        twoCycleCutPolicy = TwoCycleCutPolicy::ReverseEnd;
                        break;
                    case 3:
                        twoCycleCutPolicy = TwoCycleCutPolicy::ReverseBoth;
                        break;
                    }
                }
                case geometry::WindingRule::NonZero:
                case geometry::WindingRule::Positive:
                case geometry::WindingRule::Negative:
                    // TODO
                    break;
                }

                // geometry::isWindingNumberSatisfyingRule

                /*if (khe.startVertex() != khe.endVertex()) {
                    geometry::Vec2d p2 = khe.endVertex()->position();
                    core::Array<RingKeyHalfedge> ring2 =
                        khe.endVertex()->computeRingHalfedges();
                }*/

                // TODO
            }
        }

        VGC_ASSERT(twoCycleCutPolicy != TwoCycleCutPolicy::Auto);

        switch (twoCycleCutPolicy) {
        case TwoCycleCutPolicy::Auto:
        case TwoCycleCutPolicy::ReverseNone: {
            break;
        }
        case TwoCycleCutPolicy::ReverseStart: {
            path1.reverse();
            break;
        }
        case TwoCycleCutPolicy::ReverseEnd: {
            path2.reverse();
            break;
        }
        case TwoCycleCutPolicy::ReverseBoth: {
            path1.reverse();
            path2.reverse();
            break;
        }
        }

        path1.append(khe);
        path1.extend(path2);
        path1.append(khe.opposite());

        KeyCycle newCycle(std::move(path1));
        VGC_ASSERT(newCycle.isValid());

        kf->cycles_[cycleIndex1] = std::move(newCycle);
        kf->cycles_.removeAt(cycleIndex2);
        addToBoundary_(kf, khe.edge());

        return CutFaceResult(kf, khe.edge(), kf);
    }
}

CutFaceResult Operations::cutFaceWithClosedEdge(
    KeyFace* kf,
    KeyEdgeData&& data,
    OneCycleCutPolicy oneCycleCutPolicy) {

    KeyEdge* ke = createKeyClosedEdge(
        std::move(data), kf->parentGroup(), kf->nextSibling(), kf->time());
    return cutGlueFace(kf, ke, oneCycleCutPolicy);
}

CutFaceResult Operations::cutFaceWithOpenEdge(
    KeyFace* kf,
    KeyEdgeData&& data,
    KeyFaceVertexUsageIndex startIndex,
    KeyFaceVertexUsageIndex endIndex,
    OneCycleCutPolicy oneCycleCutPolicy,
    TwoCycleCutPolicy twoCycleCutPolicy) {

    KeyVertex* kv1 = kf->vertex(startIndex);
    KeyVertex* kv2 = kf->vertex(endIndex);
    KeyEdge* ke = createKeyOpenEdge(
        kv1, kv2, std::move(data), kf->parentGroup(), kf->nextSibling());
    KeyHalfedge khe(ke, true);
    return cutGlueFace(
        kf, khe, startIndex, endIndex, oneCycleCutPolicy, twoCycleCutPolicy);
}

CutFaceResult Operations::cutFaceWithOpenEdge(
    KeyFace* kf,
    KeyEdgeData&& data,
    KeyVertex* startVertex,
    KeyVertex* endVertex,
    OneCycleCutPolicy oneCycleCutPolicy,
    TwoCycleCutPolicy twoCycleCutPolicy) {

    KeyEdge* ke = createKeyOpenEdge(
        startVertex, endVertex, std::move(data), kf->parentGroup(), kf->nextSibling());
    return cutGlueFace(kf, ke, oneCycleCutPolicy, twoCycleCutPolicy);
}

UncutAtKeyVertexResult
Operations::uncutAtKeyVertex(KeyVertex* targetKv, bool smoothJoin) {

    UncutAtKeyVertexResult result = {};

    UncutAtKeyVertexInfo_ info = prepareUncutAtKeyVertex_(targetKv);
    if (!info.isValid) {
        return result;
    }

    KeyEdge* newKe = nullptr;

    if (info.kf) {
        // Remove Steiner vertex from face.
        //
        //       o-----------o                     o-----------o
        //       |      v    |     uncutAt(v)      |           |
        //       |     o     |    ------------>    |           |
        //       |  f        |                     |  f        |
        //       o-----------o                     o-----------o
        //
        info.kf->cycles_.removeAt(info.cycleIndex);
        removeFromBoundary_(info.kf, targetKv);
        result.resultKf = info.kf;
    }
    else if (info.khe1.edge() == info.khe2.edge()) {

        // Transform open edge into closed edge.
        //
        //             v
        //       .-----o-----.                     .-----------.
        //       |           |     uncutAt(v)      |           |
        //       |e          |    ------------>    |e'         |
        //       |           |                     |           |
        //       '-----------'                     '-----------'
        //
        //        open edge e                      closed edge e'
        // (startVertex == endVertex)
        //
        // XXX Do not create a new edge, but instead modify it in-place?
        //     This would be similar to uncut at edge that splits one cycle
        //     into two cycles in a face, without creating a new face:
        //
        //     o------o------o                     o------o------o
        //     |      |e     |                     |             |
        //     |   o--o--o   |     uncutAt(e)      |   o--o--o   |
        //     |   |     | f |    ------------>    |   |     | f |
        //     |   o-----o   |                     |   o-----o   |
        //     |             |                     |             |
        //     o-------------o                     o-------------o
        //
        KeyEdge* oldKe = info.khe1.edge();

        KeyEdgeData newData = oldKe->data();
        newData.closeStroke(smoothJoin);

        // Create new edge
        newKe = createKeyClosedEdge(
            std::move(newData), oldKe->parentGroup(), oldKe->nextSibling());
        result.resultKe = newKe;

        // Substitute all usages of old edge by new edge
        KeyHalfedge oldKhe(oldKe, true);
        KeyHalfedge newKhe(newKe, true);
        substituteEdge_(oldKhe, newKhe);

        // Since substitute expects end vertices to be the same,
        // it didn't remove our targetKv from its star's boundary.
        // So we do it manually here.
        for (Cell* cell : targetKv->star().copy()) {
            removeFromBoundary_(cell, targetKv);
        }

        // Delete old edge
        result.removedKeId1 = oldKe->id();
        hardDelete(oldKe);
    }
    else {
        // Compute new edge data as concatenation of old edges
        KeyEdgeData& ked1 = info.khe1.edge()->data();
        KeyEdgeData& ked2 = info.khe2.edge()->data();
        KeyVertex* kv1 = info.khe1.startVertex();
        KeyVertex* kv2 = info.khe2.endVertex();
        bool dir1 = info.khe1.direction();
        bool dir2 = info.khe2.direction();
        KeyEdgeData concatData;
        KeyHalfedgeData khd1(&ked1, dir1);
        KeyHalfedgeData khd2(&ked2, dir2);
        concatData = ked1.fromConcatStep(khd1, khd2, smoothJoin);

        // Determine where to insert new edge
        std::array<Node*, 2> kes = {info.khe1.edge(), info.khe2.edge()};
        Node* bottomMostEdge = findBottomMost(kes);
        Group* parentGroup = bottomMostEdge->parentGroup();
        Node* nextSibling = bottomMostEdge;

        // Create new edge e
        newKe =
            createKeyOpenEdge(kv1, kv2, std::move(concatData), parentGroup, nextSibling);
        result.resultKe = newKe;

        // Substitute all usages of (e1, e2) by e in incident faces.
        //
        // Note that we already know that the uncut is possible, which mean
        // that the face cycles never uses e1 or e2 independently, but always
        // both consecutively. In particular, we do not need to iterate on both
        // the star of e1 and e2, since they have the same star.
        //
        for (Cell* starCell : info.khe1.edge()->star().copy()) {
            KeyFace* kf = starCell->toKeyFace();
            if (!kf) {
                continue;
            }
            for (KeyCycle& cycle : kf->cycles_) {
                if (cycle.steinerVertex()) {
                    continue;
                }
                auto it = cycle.halfedges_.begin();
                while (it != cycle.halfedges_.end()) {
                    KeyHalfedge& khe = *it;
                    if (khe.edge() == info.khe1.edge()) {
                        bool dir = khe.direction() == info.khe1.direction();
                        khe = KeyHalfedge(newKe, dir);
                        ++it;
                    }
                    else if (khe.edge() == info.khe2.edge()) {
                        it = cycle.halfedges_.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
                VGC_ASSERT(cycle.isValid());
            }

            removeFromBoundary_(kf, info.khe1.edge());
            removeFromBoundary_(kf, info.khe2.edge());
            removeFromBoundary_(kf, targetKv);
            addToBoundary_(kf, newKe);
        }

        // Delete old edges
        result.removedKeId1 = info.khe1.edge()->id();
        result.removedKeId2 = info.khe2.edge()->id();
        hardDelete(info.khe1.edge());
        hardDelete(info.khe2.edge());
    }

    VGC_ASSERT(targetKv->star().isEmpty());
    hardDelete(targetKv);

    result.success = true;
    return result;
}

UncutAtKeyEdgeResult Operations::uncutAtKeyEdge(KeyEdge* targetKe) {

    UncutAtKeyEdgeResult result = {};

    UncutAtKeyEdgeInfo_ info = prepareUncutAtKeyEdge_(targetKe);
    if (!info.isValid) {
        return result;
    }

    if (targetKe->isClosed()) {
        if (info.kf1 == info.kf2) {
            // This case corresponds to removing a closed edge used twice by
            // the same face, e.g., in a cut-torus, cut-Klein bottle or
            // cut-Möbius strip.
            //
            // This doesn't make much sense in the context of vector graphics,
            // but it makes sense topologically so we support it anyway.

            KeyFace* kf = info.kf1;
            result.resultKf = kf;

            // Remove all the cycles using the closed edge. This removes:
            // - Two cycles in the case of a torus or Klein bottle,
            // - One cycle (using the edge twice) in the case of a Möbius strip.
            //
            kf->cycles_.removeIf([targetKe](const KeyCycle& cycle) {
                return !cycle.steinerVertex()
                       && cycle.halfedges().first().edge() == targetKe;
            });
            removeFromBoundary_(kf, targetKe);
        }
        else {
            // This case corresponds to removing a closed edge used once by
            // two different faces, e.g.:
            //
            //     o-------------o                     o-------------o
            //     |     e       |                     |             |
            //     |   .----.    |     uncutAt(e)      |             |
            //     |   | f1 | f2 |    ------------>    |      f      |
            //     |   '----'    |                     |             |
            //     |             |                     |             |
            //     o-------------o                     o-------------o

            // Compute cycles of new face. These are the same as all
            // the cycles from f1 and f2, except the input closed edge.
            core::Array<KeyCycle> newCycles = {};
            for (KeyCycle& cycle : info.kf1->cycles_) {
                if (cycle.steinerVertex()) {
                    newCycles.append(cycle);
                }
                else if (cycle.halfedges_.first().edge() != targetKe) {
                    newCycles.append(cycle);
                }
            }
            for (KeyCycle& cycle : info.kf2->cycles_) {
                if (cycle.steinerVertex()) {
                    newCycles.append(cycle);
                }
                else if (cycle.halfedges_.first().edge() != targetKe) {
                    newCycles.append(cycle);
                }
            }

            // Determine where to insert new face
            std::array<Node*, 2> kfs = {info.kf1, info.kf2};
            Node* bottomMostFace = findBottomMost(kfs);
            Group* parentGroup = bottomMostFace->parentGroup();
            Node* nextSibling = bottomMostFace;

            // Create new face
            KeyFace* newKf =
                createKeyFace(std::move(newCycles), parentGroup, nextSibling);
            result.resultKf = newKf;

            // Set data of the new face as concatenation of old faces
            KeyFaceData::assignFromConcatStep(
                newKf->data(), info.kf1->data(), info.kf2->data());

            // Delete old faces
            result.removedKfId1 = info.kf1->id();
            result.removedKfId2 = info.kf2->id();
            hardDelete(info.kf1);
            hardDelete(info.kf2);
        }
    }
    else { // key open edge
        if (info.kf1 == info.kf2) {
            KeyFace* kf = info.kf1;
            result.resultKf = kf;

            if (info.cycleIndex1 == info.cycleIndex2) {
                KeyCycle& cycle = kf->cycles_[info.cycleIndex1];
                Int i1 = info.componentIndex1;
                Int i2 = info.componentIndex2;
                KeyPath p1 = subPath_(cycle, i1 + 1, i2);
                KeyPath p2 = subPath_(cycle, i2 + 1, i1);
                bool d1 = cycle.halfedges_[i1].direction();
                bool d2 = cycle.halfedges_[i2].direction();

                if (d1 == d2) {
                    // Splice cycle into another cycle (Möbius strip):
                    //
                    //     o-----o---o                      o-----o---o
                    //     |     |e  |                      |         |
                    //     |   o-o-------o     uncutAt(e)   |   o-o-------o
                    //     |   |     | f |    ------------> |   |     | f |
                    //     |   o-----o   |                  |   o-----o   |
                    //     |             |                  |             |
                    //     o-------------o                  o-------------o
                    //
                    p1.extendReversed(p2);
                    kf->cycles_.append(KeyCycle(std::move(p1)));
                }
                else {
                    // Split cycle into two cycles:
                    //
                    //     o-----o-----o                     o-----o-----o
                    //     |     |e    |                     |           |
                    //     |   o-o-o   |     uncutAt(e)      |   o-o-o   |
                    //     |   |   | f |    ------------>    |   |   | f |
                    //     |   o---o   |                     |   o---o   |
                    //     |           |                     |           |
                    //     o-----------o                     o-----------o
                    //
                    kf->cycles_.append(KeyCycle(std::move(p1)));
                    kf->cycles_.append(KeyCycle(std::move(p2)));
                }
                kf->cycles_.removeAt(info.cycleIndex1);
                removeFromBoundary_(kf, targetKe);
            }
            else {
                // Splice two cycles of the same face into one cycle.
                //
                // Topologically, this corresponds to creating a torus with one
                // hole, starting from a torus with two holes that share a
                // common edge.
                //
                //    _____________          ___________          ___________
                //   ╱             ╲        ╱           ╲        ╱           ╲
                //  ╱      ___  f   ╲      ╱     ___  f  ╲      ╱     ___  f  ╲
                // (      (   )      ) -> (     (   )     ) -> (     (   )     )
                //  ╲  o---o o---o  ╱      ╲  o---o---o  ╱      ╲  o---o---o  ╱
                //   ╲ | e1| |e2 | ╱        ╲ |   |e  | ╱        ╲ |       | ╱
                //     o---o o---o    glue    o---o---o    uncut   o---o---o
                //                  (e1, e2)                (e)
                //
                //    Cylinder with       Torus with 2 holes       Torus with
                //   2 distinct holes    sharing common edge e      one hole

                // Compute new spliced cycle
                KeyCycle& cycle1 = kf->cycles_[info.cycleIndex1];
                KeyCycle& cycle2 = kf->cycles_[info.cycleIndex2];
                Int i1 = info.componentIndex1;
                Int i2 = info.componentIndex2;
                KeyPath p1 = subPath_(cycle1, i1 + 1, i1);
                KeyPath p2 = subPath_(cycle2, i2 + 1, i2);
                bool d1 = cycle1.halfedges_[i1].direction();
                bool d2 = cycle2.halfedges_[i2].direction();
                if (d1 == d2) {
                    p1.extendReversed(p2);
                }
                else {
                    p1.extend(p2);
                }
                KeyCycle newCycle(std::move(p1));

                // Add the new cycle
                kf->cycles_.append(std::move(newCycle));

                // Remove the old cycles
                auto indices = std::minmax(info.cycleIndex1, info.cycleIndex2);
                kf->cycles_.removeAt(indices.second);
                kf->cycles_.removeAt(indices.first);
                removeFromBoundary_(kf, targetKe);
            }
        }
        else { // f1 != f2

            // Splice two cycles of different faces into one cycle, merging the
            // two faces f1 and f2 into one new face.
            //
            // o--------o--------o                 o--------o--------o
            // |        |        |   uncutAt(e)    |                 |
            // |   f1   |e  f2   |  ------------>  |        f        |
            // |        |        |                 |                 |
            // o--------o--------o                 o--------o--------o

            // Compute new spliced cycle
            KeyFace* kf1 = info.kf1;
            KeyFace* kf2 = info.kf2;
            KeyCycle& cycle1 = kf1->cycles_[info.cycleIndex1];
            KeyCycle& cycle2 = kf2->cycles_[info.cycleIndex2];
            Int i1 = info.componentIndex1;
            Int i2 = info.componentIndex2;
            KeyPath p1 = subPath_(cycle1, i1 + 1, i1);
            KeyPath p2 = subPath_(cycle2, i2 + 1, i2);
            bool d1 = cycle1.halfedges_[i1].direction();
            bool d2 = cycle2.halfedges_[i2].direction();
            if (d1 == d2) {
                p1.extendReversed(p2);
            }
            else {
                p1.extend(p2);
            }
            KeyCycle newCycle(std::move(p1));

            // Compute cycles of new face. These are the same as all the cycles
            // from f1 and f2, except that we remove the two old cycles that
            // were using e, and add the new spliced cycle and
            //
            core::Array<KeyCycle> newCycles;
            for (Int j = 0; j < kf1->cycles_.length(); ++j) {
                if (j != info.cycleIndex1) {
                    newCycles.append(kf1->cycles_[j]);
                }
            }
            for (Int j = 0; j < kf2->cycles_.length(); ++j) {
                if (j != info.cycleIndex2) {
                    newCycles.append(kf2->cycles_[j]);
                }
            }
            newCycles.append(std::move(newCycle));

            // Determine where to insert new face
            std::array<Node*, 2> kfs = {info.kf1, info.kf2};
            Node* bottomMostFace = findBottomMost(kfs);
            Group* parentGroup = bottomMostFace->parentGroup();
            Node* nextSibling = bottomMostFace;

            // Create new face
            KeyFace* newKf =
                createKeyFace(std::move(newCycles), parentGroup, nextSibling);
            result.resultKf = newKf;

            // Set data of the new face as concatenation of old faces
            KeyFaceData::assignFromConcatStep(
                newKf->data(), info.kf1->data(), info.kf2->data());

            // Delete old faces
            result.removedKfId1 = info.kf1->id();
            result.removedKfId2 = info.kf2->id();
            hardDelete(info.kf1);
            hardDelete(info.kf2);
        }
    }

    VGC_ASSERT(targetKe->star().isEmpty());
    hardDelete(targetKe);

    result.success = true;
    return result;
}

void Operations::moveToGroup(Node* node, Group* parentGroup, Node* nextSibling) {
    if (nextSibling) {
        insertNodeBeforeSibling_(node, nextSibling);
    }
    else {
        insertNodeAsLastChild_(node, parentGroup);
    }
}

void Operations::moveBelowBoundary(Node* node) {
    Cell* cell = node->toCell();
    if (!cell) {
        return;
    }
    auto boundary = cell->boundary();
    if (boundary.isEmpty()) {
        // nothing to do.
        return;
    }
    // currently keeping the same parent
    Node* oldParentNode = cell->parentGroup();
    Node* newParentNode = oldParentNode;
    if (!newParentNode) {
        // `boundary.length() > 0` previously checked.
        newParentNode = (*boundary.begin())->parentGroup();
    }
    if (!newParentNode) {
        return;
    }
    Group* newParent = newParentNode->toGroupUnchecked();
    Node* nextSibling = newParent->firstChild();
    while (nextSibling) {
        bool found = false;
        for (Cell* boundaryCell : boundary) {
            if (nextSibling == boundaryCell) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        nextSibling = nextSibling->nextSibling();
    }
    if (nextSibling) {
        insertNodeBeforeSibling_(node, nextSibling);
    }
    else {
        // all boundary cells are in another group
        // TODO: use set of ancestors of boundary cells
        insertNodeAsLastChild_(node, newParent);
    }
}

// dev note: update boundary before star

void Operations::setKeyVertexPosition(KeyVertex* kv, const geometry::Vec2d& pos) {
    if (pos == kv->position_) {
        // same data
        return;
    }

    kv->position_ = pos;

    onGeometryChanged_(kv);
}

void Operations::onNodeCreated_(Node* node) {
    complex_->opDiff_.onNodeCreated_(node);
}

void Operations::onNodeDestroyed_(core::Id nodeId) {
    complex_->opDiff_.onNodeDestroyed_(nodeId);
}

void Operations::onNodeInserted_(
    Node* node,
    Node* oldParent,
    NodeInsertionType insertionType) {

    complex_->opDiff_.onNodeInserted_(node, oldParent, insertionType);
}

void Operations::onNodeModified_(Node* node, NodeModificationFlags diffFlags) {
    complex_->opDiff_.onNodeModified_(node, diffFlags);
}

void Operations::onNodePropertyModified_(Node* node, core::StringId name) {
    complex_->opDiff_.onNodePropertyModified_(node, name);
}

void Operations::insertNodeBeforeSibling_(Node* node, Node* nextSibling) {
    Group* oldParent = node->parentGroup();
    Group* newParent = nextSibling->parentGroup();
    if (newParent->insertChildUnchecked(nextSibling, node)) {
        onNodeInserted_(node, oldParent, NodeInsertionType::BeforeSibling);
    }
}

void Operations::insertNodeAfterSibling_(Node* node, Node* previousSibling) {
    Group* oldParent = node->parentGroup();
    Group* newParent = previousSibling->parentGroup();
    Node* nextSibling = previousSibling->nextSibling();
    if (newParent->insertChildUnchecked(nextSibling, node)) {
        onNodeInserted_(node, oldParent, NodeInsertionType::AfterSibling);
    }
}

void Operations::insertNodeAsFirstChild_(Node* node, Group* parent) {
    Group* oldParent = node->parentGroup();
    Node* nextSibling = parent->firstChild();
    if (parent->insertChildUnchecked(nextSibling, node)) {
        onNodeInserted_(node, oldParent, NodeInsertionType::FirstChild);
    }
}

void Operations::insertNodeAsLastChild_(Node* node, Group* parent) {
    Group* oldParent = node->parentGroup();
    if (parent->appendChild(node)) {
        onNodeInserted_(node, oldParent, NodeInsertionType::LastChild);
    }
}

Node* Operations::findTopMost(core::ConstSpan<Node*> nodes) {
    // currently only looking under a single parent
    // TODO: tree-wide top most.
    if (nodes.isEmpty()) {
        return nullptr;
    }
    Node* node0 = nodes[0];
    Group* parent = node0->parentGroup();
    Node* topMostNode = parent->lastChild();
    while (topMostNode) {
        if (nodes.contains(topMostNode)) {
            break;
        }
        topMostNode = topMostNode->previousSibling();
    }
    return topMostNode;
}

Node* Operations::findBottomMost(core::ConstSpan<Node*> nodes) {
    // currently only looking under a single parent
    // TODO: tree-wide bottom most.
    if (nodes.isEmpty()) {
        return nullptr;
    }
    Node* node0 = nodes[0];
    Group* parent = node0->parentGroup();
    Node* bottomMostNode = parent->firstChild();
    while (bottomMostNode) {
        if (nodes.contains(bottomMostNode)) {
            break;
        }
        bottomMostNode = bottomMostNode->nextSibling();
    }
    return bottomMostNode;
}

template<typename TNodeRange>
void Operations::deleteWithDependents_(
    TNodeRange nodes,
    bool deleteIsolatedVertices,
    bool tryRepairingStarCells) {

    Group* rootGroup = complex()->rootGroup();

    // First collect all descendants
    std::unordered_set<Node*> descendants;

    for (Node* node : nodes) {
        for (Node* descendant : node->descendants()) {
            descendants.insert(descendant);
        }
        // When hard-deleting the root, we delete all nodes below the root, but
        // preserve the root itself since we have the invariant that there is
        // always a root.
        //
        if (node != rootGroup) {
            descendants.insert(node);
        }
    }

    delete_(descendants, deleteIsolatedVertices, tryRepairingStarCells);
}

void Operations::delete_(
    std::unordered_set<Node*> descendants,
    bool deleteIsolatedVertices,
    bool tryRepairingStarCells) {

    std::unordered_set<Node*> nodesToDestroy;

    // Flag all descendants as about to be deleted.
    //
    for (Node* descendant : descendants) {
        descendant->isBeingDeleted_ = true;
        nodesToDestroy.insert(descendant);
    }

    // Delete star of nodes.
    core::Array<KeyFace*> starFaces;
    for (Node* descendant : descendants) {
        Cell* cell = descendant->toCell();
        if (!cell) {
            continue;
        }
        for (Cell* starCell : cell->star()) {
            switch (starCell->cellType()) {
            case CellType::KeyFace: {
                KeyFace* kf = starCell->toKeyFaceUnchecked();
                if (!starFaces.contains(kf)) {
                    starFaces.append(kf);
                }
                break;
            }
            default: {
                if (!starCell->isBeingDeleted_) {
                    starCell->isBeingDeleted_ = true;
                    nodesToDestroy.insert(starCell);
                }
                break;
            }
            }
        }
    }

    // TODO: use KeyFace winding rule.
    geometry::WindingRule windingRule = geometry::WindingRule::Odd;
    constexpr Int numSamplesPerContainTest = 20;
    constexpr double ratioThreshold = 0.5;

    KeyCycle tmpCycle;
    for (KeyFace* kf : starFaces) {
        if (kf->isBeingDeleted_) {
            continue;
        }
        if (!tryRepairingStarCells) {
            kf->isBeingDeleted_ = true;
            nodesToDestroy.insert(kf);
            continue;
        }

        struct RepairedCycle {
            KeyCycle cycle;
            Int originalIndex;
            bool isUnchanged;
        };
        core::Array<RepairedCycle> repairedCycles;

        for (Int i = 0; i != kf->cycles_.length(); ++i) {
            const KeyCycle& kc = kf->cycles_[i];
            KeyVertex* sv = kc.steinerVertex();
            if (sv) {
                if (!sv->isBeingDeleted_) {
                    repairedCycles.append({kc, i, true});
                }
                continue;
            }
            tmpCycle = kc;
            Int numRemoved = tmpCycle.halfedges_.removeIf(
                [](const KeyHalfedge& he) { return he.edge()->isBeingDeleted_; });
            bool isUnchanged = numRemoved == 0;
            if (isUnchanged || tmpCycle.isValid()) {
                repairedCycles.append({tmpCycle, i, isUnchanged});
            }
        }

        // If a repaired cycle is contained in a deleted one, delete it.
        for (auto it = repairedCycles.begin(); it != repairedCycles.end();) {
            const KeyCycle& repairedCycle = it->cycle;
            KeyVertex* steinerVertex = repairedCycle.steinerVertex();
            if (steinerVertex) {
                bool keep = true;
                geometry::Vec2d pos = steinerVertex->position();
                for (Int i = 0; i != kf->cycles_.length(); ++i) {
                    const KeyCycle& originalCycle = kf->cycles_[i];
                    RepairedCycle* rc = repairedCycles.search(
                        [i](const RepairedCycle& rc) { return rc.originalIndex == i; });
                    if (rc && rc->isUnchanged) {
                        continue;
                    }
                    if (originalCycle.interiorContains(pos, windingRule)) {
                        if (!rc || !rc->cycle.interiorContains(pos, windingRule)) {
                            keep = false;
                            break;
                        }
                    }
                }
                if (!keep) {
                    it = repairedCycles.erase(it);
                }
                else {
                    ++it;
                }
            }
            else {
                bool keep = true;
                core::Array<geometry::Vec2d> samples =
                    repairedCycle.sampleUniformly(numSamplesPerContainTest);
                for (Int i = 0; i != kf->cycles_.length(); ++i) {
                    const KeyCycle& originalCycle = kf->cycles_[i];
                    RepairedCycle* rc = repairedCycles.search(
                        [=](const RepairedCycle& rc) { return rc.originalIndex == i; });
                    if (rc && rc->isUnchanged) {
                        continue;
                    }
                    double ratio =
                        originalCycle.interiorContainedRatio(samples, windingRule);
                    if (ratio > ratioThreshold) {
                        if (!rc) {
                            keep = false;
                            break;
                        }
                        double ratio2 =
                            rc->cycle.interiorContainedRatio(samples, windingRule);
                        if (ratio2 <= ratioThreshold) {
                            keep = false;
                            break;
                        }
                    }
                }
                if (!keep) {
                    repairedCycles.erase(it);
                    // restart tests since previous cycles could have been
                    // contained and thus saved by the current one.
                    it = repairedCycles.begin();
                }
                else {
                    ++it;
                }
            }
        }

        if (repairedCycles.isEmpty()) {
            kf->isBeingDeleted_ = true;
            nodesToDestroy.insert(kf);
        }
        else {
            for (Cell* boundaryCell : kf->boundary().copy()) {
                removeFromBoundary_(kf, boundaryCell);
            }
            kf->cycles_.clear();
            for (RepairedCycle& repairedCycle : repairedCycles) {
                addToBoundary_(kf, repairedCycle.cycle);
                kf->cycles_.append(std::move(repairedCycle.cycle));
            }
        }
    }

    // Helper function that tests if the star of a cell will become empty after
    // deleting all cells flagged for deletion.
    //
    auto hasEmptyStar = [](Cell* cell) {
        bool isStarEmpty = true;
        for (Cell* starCell : cell->star()) {
            if (!starCell->isBeingDeleted_) {
                isStarEmpty = false;
                break;
            }
        }
        return isStarEmpty;
    };

    // Update star of cells in the boundary of deleted cells.
    //
    // For example, if we delete an edge, we should remove the edge
    // for the star of its end vertices.
    //
    // In this step, we also detect vertices which are about to become
    // isolated, and delete these if deleteIsolatedVertices is true. Note that
    // there is no need to collectDependentNodes_(isolatedVertex), since being
    // isolated means having an empty star, which means that the vertex has no
    // dependent nodes.
    //
    // Note: we store the isolated vertices as set<Node*> instead of set<Cell*>
    // so that we can later merge with nodesToDelete.
    //
    std::unordered_set<Node*> isolatedKeyVertices;
    std::unordered_set<Node*> isolatedInbetweenVertices;
    for (Node* nodeToDestroy : nodesToDestroy) {
        if (nodeToDestroy->isCell()) {
            Cell* cell = nodeToDestroy->toCellUnchecked();
            for (Cell* boundaryCell : cell->boundary().copy()) {
                if (boundaryCell->isBeingDeleted_) {
                    continue;
                }
                if (deleteIsolatedVertices
                    && boundaryCell->spatialType() == CellSpatialType::Vertex
                    && hasEmptyStar(boundaryCell)) {

                    switch (boundaryCell->cellType()) {
                    case CellType::KeyVertex:
                        isolatedKeyVertices.insert(boundaryCell);
                        break;
                    case CellType::InbetweenVertex:
                        isolatedInbetweenVertices.insert(boundaryCell);
                        break;
                    default:
                        break;
                    }
                    boundaryCell->isBeingDeleted_ = true;
                }
                if (!boundaryCell->isBeingDeleted_) {
                    boundaryCell->star_.removeOne(cell);
                    onNodeModified_(boundaryCell, NodeModificationFlag::StarChanged);
                }
            }
            cell->star_.clear();
        }
    }

    // Deleting isolated inbetween vertices might indirectly cause key vertices
    // to become isolated, so we detect these in a second pass.
    //
    //       ke1
    // kv1 -------- kv2          Scenario: user hard deletes ie1
    //  |            |
    //  |iv1         | iv2        -> This directly makes iv1, iv2, and iv3 isolated
    //  |            |               (but does not directly make kv5 isolated, since
    //  |    ie1     kv5              the star of kv5 still contained iv2 and iv3)
    //  |            |
    //  |            | iv3
    //  |            |
    // kv3 ------- kv4
    //       ke2
    //
    if (deleteIsolatedVertices) {
        for (Node* inbetweenVertexNode : isolatedInbetweenVertices) {
            Cell* inbetweenVertex = inbetweenVertexNode->toCellUnchecked();
            for (Cell* keyVertex : inbetweenVertex->boundary()) {
                if (keyVertex->isBeingDeleted_) {
                    continue;
                }
                if (hasEmptyStar(keyVertex)) {
                    isolatedKeyVertices.insert(keyVertex);
                    keyVertex->isBeingDeleted_ = true;
                }
                else {
                    keyVertex->star_.removeOne(inbetweenVertex);
                    onNodeModified_(keyVertex, NodeModificationFlag::StarChanged);
                }
            }
        }
        nodesToDestroy.merge(isolatedKeyVertices);
        nodesToDestroy.merge(isolatedInbetweenVertices);
    }

    core::Array<Node*> nodesToDestroyArray(nodesToDestroy);
    destroyNodes_(nodesToDestroyArray);
}

// Assumes node has no children.
// maybe we should also handle star/boundary changes here
void Operations::destroyChildlessNode_(Node* node) {
    [[maybe_unused]] Group* group = node->toGroup();
    if (group) {
        VGC_ASSERT(group->numChildren() == 0);
    }
    Group* parentGroup = node->parentGroup();
    if (parentGroup) {
        node->unparent();
        onNodeModified_(parentGroup, NodeModificationFlag::ChildrenChanged);
    }
    if (node->isCell()) {
        complex_->temporaryCellSet_.removeOne(node->toCellUnchecked());
    }
    onNodeDestroyed_(node->id());
    complex_->nodes_.erase(node->id());
}

// Assumes that all descendants of all `nodes` are also in `nodes`.
void Operations::destroyNodes_(core::ConstSpan<Node*> nodes) {
    // debug check
    for (Node* node : nodes) {
        Group* group = node->toGroup();
        if (group) {
            for (Node* child : *group) {
                VGC_ASSERT(nodes.contains(child));
            }
        }
    }
    for (Node* node : nodes) {
        Group* parentGroup = node->parentGroup();
        if (parentGroup) {
            node->unparent();
            onNodeModified_(parentGroup, NodeModificationFlag::ChildrenChanged);
        }
    }
    for (Node* node : nodes) {
        onNodeDestroyed_(node->id());
        complex_->nodes_.erase(node->id());
    }
}

void Operations::onGeometryChanged_(Cell* cell) {
    onNodeModified_(cell, NodeModificationFlag::GeometryChanged);
    for (Cell* starCell : cell->star()) { // See [1]
        onNodeModified_(starCell, NodeModificationFlag::BoundaryGeometryChanged);
    }
    // Note: no need for recursion in the `cell->star()` loops below, since
    // starCell->star() is a subset of cell->star().
}

namespace {

void checkAddToBoundaryArgs_(Cell* boundedCell, Cell* boundingCell) {
    if (!boundingCell) {
        throw core::LogicError("Cannot add or remove null cell to boundary.");
    }
    if (!boundedCell) {
        throw core::LogicError("Cannot modify the boundary of a null cell.");
    }
}

} // namespace

void Operations::addToBoundary_(Cell* boundedCell, Cell* boundingCell) {
    checkAddToBoundaryArgs_(boundedCell, boundingCell);
    if (!boundedCell->boundary_.contains(boundingCell)) {
        boundedCell->boundary_.append(boundingCell);
        boundingCell->star_.append(boundedCell);
        onBoundaryChanged_(boundedCell, boundingCell);
    }
}

void Operations::removeFromBoundary_(Cell* boundedCell, Cell* boundingCell) {
    checkAddToBoundaryArgs_(boundedCell, boundingCell);
    if (boundedCell->boundary_.contains(boundingCell)) {
        boundedCell->boundary_.removeOne(boundingCell);
        boundingCell->star_.removeOne(boundedCell);
        onBoundaryChanged_(boundedCell, boundingCell);
    }
}

void Operations::onBoundaryChanged_(Cell* boundedCell, Cell* boundingCell) {
    onNodeModified_(
        boundedCell,
        {NodeModificationFlag::BoundaryChanged,
         NodeModificationFlag::BoundaryGeometryChanged});
    onNodeModified_(boundingCell, NodeModificationFlag::StarChanged);
}

void Operations::addToBoundary_(FaceCell* face, const KeyCycle& cycle) {
    if (cycle.steinerVertex()) {
        // Steiner cycle
        addToBoundary_(face, cycle.steinerVertex());
    }
    else if (cycle.halfedges().first().isClosed()) {
        // Simple cycle
        addToBoundary_(face, cycle.halfedges().first().edge());
    }
    else {
        // Non-simple cycle
        for (const KeyHalfedge& halfedge : cycle.halfedges()) {
            addToBoundary_(face, halfedge.edge());
            addToBoundary_(face, halfedge.endVertex());
        }
    }
}

void Operations::substituteVertex_(KeyVertex* oldVertex, KeyVertex* newVertex) {
    if (newVertex == oldVertex) {
        return;
    }
    for (Cell* starCell : oldVertex->star().copy()) {
        starCell->substituteKeyVertex_(oldVertex, newVertex);
        removeFromBoundary_(starCell, oldVertex);
        addToBoundary_(starCell, newVertex);
    }
}

// It assumes end vertices are the same!
void Operations::substituteEdge_(const KeyHalfedge& oldKhe, const KeyHalfedge& newKhe) {
    if (oldKhe == newKhe) {
        return;
    }
    KeyEdge* const oldKe = oldKhe.edge();
    KeyEdge* const newKe = newKhe.edge();
    for (Cell* starCell : oldKe->star().copy()) {
        starCell->substituteKeyEdge_(oldKhe, newKhe);
        removeFromBoundary_(starCell, oldKe);
        addToBoundary_(starCell, newKe);
    }
}

KeyEdge* Operations::glueKeyOpenEdges_(core::ConstSpan<KeyHalfedge> khs) {

    if (khs.isEmpty()) {
        return nullptr;
    }

    Int n = khs.length();
    core::Array<KeyHalfedgeData> khds;
    khds.reserve(n);
    for (const KeyHalfedge& kh : khs) {
        KeyEdgeData& kd = kh.edge()->data();
        khds.emplaceLast(&kd, kh.direction());
    }
    KeyEdgeData newData = KeyEdgeData::fromGlueOpen(khds);
    VGC_ASSERT(newData.stroke());

    std::array<geometry::Vec2d, 2> endPositions = newData.stroke()->endPositions();

    core::Array<KeyVertex*> startVertices;
    startVertices.reserve(n);
    for (const KeyHalfedge& kh : khs) {
        startVertices.append(kh.startVertex());
    }
    KeyVertex* startKv = glueKeyVertices(startVertices, endPositions[0]);

    // Note: we can only list end vertices after the glue of
    // start vertices since it can substitute end vertices.
    core::Array<KeyVertex*> endVertices;
    endVertices.reserve(n);
    for (const KeyHalfedge& kh : khs) {
        endVertices.append(kh.endVertex());
    }
    geometry::Vec2d endVertexPosition = endPositions[1];
    if (endVertices.contains(startKv)) {
        // collapsing start and end to single vertex
        endVertexPosition = 0.5 * (endPositions[0] + endVertexPosition);
        newData.snapGeometry(endVertexPosition, endVertexPosition);
        startKv = nullptr;
    }
    KeyVertex* endKv = glueKeyVertices(endVertices, endVertexPosition);
    if (!startKv) {
        startKv = endKv;
    }

    // Location: top-most input edge
    core::Array<Node*> edgeNodes(n);
    for (Int i = 0; i < n; ++i) {
        edgeNodes[i] = khs[i].edge();
    }

    Node* topMostEdge = findTopMost(edgeNodes);
    Group* parentGroup = topMostEdge->parentGroup();
    Node* nextSibling = topMostEdge->nextSibling();

    KeyEdge* newKe =
        createKeyOpenEdge(startKv, endKv, std::move(newData), parentGroup, nextSibling);

    KeyHalfedge newKh(newKe, true);
    for (const KeyHalfedge& kh : khs) {
        substituteEdge_(kh, newKh);
        // It is important that no 2 halfedges refer to the same edge.
        hardDelete(kh.edge(), true);
    }

    return newKe;
}

KeyEdge* Operations::glueKeyClosedEdges_(
    core::ConstSpan<KeyHalfedge> khs,
    core::ConstSpan<double> uOffsets) {

    if (khs.isEmpty()) {
        return nullptr;
    }

    // Location: top-most input edge

    Int n = khs.length();
    core::Array<Node*> edgeNodes;
    edgeNodes.reserve(n);
    core::Array<KeyHalfedgeData> khds;
    khds.reserve(n);
    for (const KeyHalfedge& kh : khs) {
        edgeNodes.append(kh.edge());
        KeyEdgeData& kd = kh.edge()->data();
        khds.emplaceLast(&kd, kh.direction());
    }

    Node* topMostEdge = findTopMost(edgeNodes);
    Group* parentGroup = topMostEdge->parentGroup();
    Node* nextSibling = topMostEdge->nextSibling();

    KeyEdgeData newData = KeyEdgeData::fromGlueClosed(khds, uOffsets);
    VGC_ASSERT(newData.stroke());

    KeyEdge* newKe = createKeyClosedEdge(std::move(newData), parentGroup, nextSibling);

    KeyHalfedge newKh(newKe, true);
    for (const KeyHalfedge& kh : khs) {
        substituteEdge_(kh, newKh);
        // It is important that no 2 halfedges refer to the same edge.
        hardDelete(kh.edge(), true);
    }

    return newKe;
}

KeyPath
Operations::subPath_(const KeyCycle& cycle, Int first, Int last, bool loopIfEmptyRange) {
    if (cycle.steinerVertex()) {
        return KeyPath(cycle.steinerVertex());
    }
    Int n = cycle.halfedges_.length();
    first = ((first % n) + n) % n;
    last = ((last % n) + n) % n;
    if (first == last) {
        if (loopIfEmptyRange) {
            return rotatedCycleHalfedges_(cycle, first);
        }
        else {
            KeyVertex* singleVertex = cycle.halfedges_[first].startVertex();
            return KeyPath(singleVertex);
        }
    }
    else {
        core::Array<KeyHalfedge> halfedges;
        for (Int i = first; i != last; i = (i + 1) % n) {
            halfedges.append(cycle.halfedges_[i]);
        }
        return KeyPath(std::move(halfedges));
    }
}

// Note: Uncut does not yet support incident inbetween cells. As a
// workaround, we do nothing, as if uncutting here isn't possible, even
// though maybe in theory it is. In the future, we should handle the cases
// where uncutting is actually possible despite the presence of incident
// inbetween cells.
Operations::UncutAtKeyVertexInfo_ Operations::prepareUncutAtKeyVertex_(KeyVertex* kv) {
    UncutAtKeyVertexInfo_ result = {};

    for (Cell* starCell : kv->star()) {
        switch (starCell->cellType()) {
        case CellType::KeyEdge: {
            KeyEdge* ke = starCell->toKeyEdgeUnchecked();
            if (ke->isStartVertex(kv)) {
                if (!result.khe1.edge()) {
                    result.khe1 = KeyHalfedge(ke, false);
                }
                else if (!result.khe2.edge()) {
                    result.khe2 = KeyHalfedge(ke, true);
                }
                else {
                    // Cannot uncut if kv is used more than twice as edge vertex.
                    return result;
                }
            }
            if (ke->isEndVertex(kv)) {
                if (!result.khe1.edge()) {
                    result.khe1 = KeyHalfedge(ke, true);
                }
                else if (!result.khe2.edge()) {
                    result.khe2 = KeyHalfedge(ke, false);
                }
                else {
                    // Cannot uncut if kv is used more than twice as edge vertex.
                    return result;
                }
            }
            break;
        }
        case CellType::KeyFace: {
            KeyFace* kf = starCell->toKeyFaceUnchecked();
            Int cycleIndex = -1;
            for (const KeyCycle& cycle : kf->cycles()) {
                ++cycleIndex;
                if (cycle.steinerVertex() == kv) {
                    if (result.kf) {
                        // Cannot uncut if kv is used more than once as steiner vertex.
                        return result;
                    }
                    result.kf = kf;
                    result.cycleIndex = cycleIndex;
                }
            }
            break;
        }
        case CellType::InbetweenVertex: {
            //InbetweenVertex* iv = starCell->toInbetweenVertexUnchecked();
            // Currently not supported.
            return result;
            break;
        }
        default:
            break;
        }
    }

    if (result.khe1.edge()) {
        if (!result.kf && result.khe2.edge()) {
            if (result.khe1.edge() != result.khe2.edge()) {
                // If edges are different:
                // (inverse op: cut open edge)
                //
                //                     ┌─←─┐
                //                     │   C
                // o ───A──→ X ───B──→ o ──┘
                //
                // Uncutting at X means replacing the chain AB by D.
                // Thus the cycle B*A*ABC would become D*DC but
                // the cycle B*BC would not be representable anymore.
                //
                // In other words, we want the edges to always be used
                // consecutively in the cycles they are part of.
                //
                for (Cell* starCell : kv->star()) {
                    KeyFace* kf = starCell->toKeyFace();
                    if (!kf) {
                        continue;
                    }
                    for (const KeyCycle& cycle : kf->cycles()) {
                        if (cycle.steinerVertex()) {
                            continue;
                        }
                        KeyEdge* previousKe = cycle.halfedges().last().edge();
                        for (const KeyHalfedge& khe : cycle.halfedges()) {
                            if (khe.startVertex() == kv) {
                                if (khe.edge() == previousKe) {
                                    // Cannot uncut if kv is used as a u-turn in cycle.
                                    return result;
                                }
                            }
                            previousKe = khe.edge();
                        }
                    }
                }
                result.isValid = true;
            }
            else {
                // (inverse op: cut closed edge)
                // the only incident edge is a loop, and we don't
                // want kv to be used as a u-turn in any cycle.
                for (Cell* starCell : kv->star()) {
                    KeyFace* kf = starCell->toKeyFace();
                    if (!kf) {
                        continue;
                    }
                    for (const KeyCycle& cycle : kf->cycles()) {
                        if (cycle.steinerVertex()) {
                            continue;
                        }
                        if (cycle.halfedges().first().edge() != result.khe1.edge()) {
                            continue;
                        }
                        // All edges in this cycle are equal to result.khe1.edge().
                        // We require them to be in the same direction (no u-turn).
                        bool direction = cycle.halfedges().first().direction();
                        for (const KeyHalfedge& khe : cycle.halfedges()) {
                            if (khe.direction() != direction) {
                                // Cannot uncut if kv is used as a u-turn in cycle.
                                return result;
                            }
                        }
                    }
                }
                result.isValid = true;
            }
        }
    }
    else if (result.kf) {
        // (inverse op: cut face at vertex)
        result.isValid = true;
    }

    return result;
}

Operations::UncutAtKeyEdgeInfo_ Operations::prepareUncutAtKeyEdge_(KeyEdge* ke) {
    UncutAtKeyEdgeInfo_ result = {};

    for (Cell* starCell : ke->star()) {
        switch (starCell->cellType()) {
        case CellType::KeyFace: {
            KeyFace* kf = starCell->toKeyFaceUnchecked();
            Int cycleIndex = -1;
            for (const KeyCycle& cycle : kf->cycles()) {
                ++cycleIndex;
                if (cycle.steinerVertex()) {
                    continue;
                }
                Int componentIndex = -1;
                for (const KeyHalfedge& khe : cycle.halfedges()) {
                    ++componentIndex;
                    if (khe.edge() != ke) {
                        continue;
                    }
                    if (!result.kf1) {
                        result.kf1 = kf;
                        result.cycleIndex1 = cycleIndex;
                        result.componentIndex1 = componentIndex;
                    }
                    else if (!result.kf2) {
                        result.kf2 = kf;
                        result.cycleIndex2 = cycleIndex;
                        result.componentIndex2 = componentIndex;
                    }
                    else {
                        // Cannot uncut if used more than twice as face cycle component.
                        return result;
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }

    if (result.kf1 && result.kf2) {
        result.isValid = true;
    }

    return result;
}

Int Operations::countSteinerUses_(KeyVertex* kv) {
    Int count = 0;
    for (Cell* starCell : kv->star()) {
        KeyFace* kf = starCell->toKeyFace();
        if (!kf) {
            continue;
        }
        for (const KeyCycle& cycle : kf->cycles()) {
            if (cycle.steinerVertex() == kv) {
                ++count;
            }
        }
    }
    return count;
}

Int Operations::countUses_(KeyVertex* kv) {
    Int count = 0;
    for (Cell* starCell : kv->star()) {
        switch (starCell->cellType()) {
        case CellType::KeyEdge: {
            KeyEdge* ke = starCell->toKeyEdgeUnchecked();
            bool hasFaceInStar = false;
            for (Cell* keStarCell : ke->star()) {
                if (keStarCell->cellType() == CellType::KeyFace) {
                    hasFaceInStar = true;
                    break;
                }
            }
            if (!hasFaceInStar) {
                if (ke->isStartVertex(kv)) {
                    ++count;
                }
                if (ke->isEndVertex(kv)) {
                    ++count;
                }
            }
            break;
        }
        case CellType::KeyFace: {
            KeyFace* kf = starCell->toKeyFaceUnchecked();
            for (const KeyCycle& cycle : kf->cycles()) {
                if (cycle.steinerVertex()) {
                    if (cycle.steinerVertex() == kv) {
                        ++count;
                    }
                    continue;
                }
                for (const KeyHalfedge& khe : cycle.halfedges()) {
                    if (khe.startVertex() == kv) {
                        ++count;
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return count;
}

Int Operations::countUses_(KeyEdge* ke) {
    Int count = 0;
    for (Cell* starCell : ke->star()) {
        KeyFace* kf = starCell->toKeyFace();
        if (!kf) {
            continue;
        }
        for (const KeyCycle& cycle : kf->cycles()) {
            if (cycle.steinerVertex()) {
                continue;
            }
            for (const KeyHalfedge& khe : cycle.halfedges()) {
                if (khe.edge() == ke) {
                    ++count;
                }
            }
        }
    }
    return count;
}

} // namespace detail

} // namespace vgc::vacomplex
