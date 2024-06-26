// Copyright 2021 The VGC Developers
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

#include <vgc/dom/element.h>

#include <vgc/core/logging.h>
#include <vgc/dom/document.h>
#include <vgc/dom/logcategories.h>
#include <vgc/dom/noneor.h>
#include <vgc/dom/operation.h>
#include <vgc/dom/schema.h>
#include <vgc/dom/strings.h>

namespace vgc::dom {

Element::Element(CreateKey key, PrivateKey, Document* document, core::StringId tagName)
    : Node(key, ProtectedKey{}, document, NodeType::Element)
    , tagName_(tagName)
    , internalId_(core::genId()) {

    document->elementByInternalIdMap_[internalId_] = this;
}

void Element::onDestroyed() {
    document()->onElementAboutToBeDestroyed_(this);
    id_ = core::StringId();
    SuperClass::onDestroyed();
}

Element* Element::create_(Node* parent, core::StringId tagName, Element* nextSibling) {
    Document* doc = parent->document();

    ElementPtr e = core::createObject<Element>(PrivateKey{}, doc, tagName);
    e->insertObjectToParent_(parent, nextSibling);
    core::History::do_<CreateElementOperation>(
        doc->history(), e.get(), parent, nextSibling);

    return e.get();
}

Element* Element::createCopy_(Node* parent, const Element* source, Element* nextSibling) {

    Document* srcDoc = source->document();
    Document* tgtDoc = parent->document();

    if (parent->isDescendantOf(source)) {
        return nullptr;
    }

    srcDoc->preparePathsUpdateRec_(srcDoc);

    detail::PathUpdateData pud = {};
    Element* result = createCopy_(parent, source, nextSibling, pud);

    tgtDoc->updatePathsRec_(tgtDoc, pud);

    return result;
}

Element* Element::createCopy_(
    Node* parent,
    const Element* source,
    Element* nextSibling,
    detail::PathUpdateData& pud) {

    Document* doc = parent->document();

    Element* e = createCopyRec_(parent, source, nextSibling, pud);
    core::History::do_<CreateElementOperation>(doc->history(), e, parent, nextSibling);

    return e;
}

Element* Element::createCopyRec_(
    Node* parent,
    const Element* source,
    Element* nextSibling,
    detail::PathUpdateData& pud) {

    Document* doc = parent->document();

    ElementPtr e = core::createObject<Element>(PrivateKey{}, doc, source->tagName_);
    e->insertObjectToParent_(parent, nextSibling);
    e->name_ = source->name_;
    e->authoredAttributes_ = source->authoredAttributes_;

    pud.addCopiedElement(source->internalId(), e->internalId());

    core::StringId id = source->id_;
    auto& elementByIdMap = parent->document()->elementByIdMap_;
    if (elementByIdMap.try_emplace(id, e.get()).second) {
        // TODO: implement Document::sanitizeNewId(id)
        e->id_ = id;
    }
    else {
        // resolve id conflict by not copying the id
        e->authoredAttributes_.removeOneIf(
            [](const AuthoredAttribute& attr) { return attr.name() == strings::id; });
    }

    for (Node* child : source->children()) {
        // TODO: copy other things than Element too.
        Element* childElement = Element::cast(child);
        if (childElement) {
            createCopyRec_(e.get(), childElement, nullptr, pud);
        }
    }

    return e.get();
}

Element* Element::create(Document* parent, core::StringId tagName) {
    if (parent->rootElement()) {
        throw SecondRootElementError(parent);
    }

    return create_(parent, tagName, nullptr);
}

Element* Element::create(Element* parent, core::StringId tagName, Element* nextSibling) {
    return create_(parent, tagName, nextSibling);
}

Element* Element::createCopy(Document* parent, const Element* source) {
    if (parent->rootElement()) {
        throw SecondRootElementError(parent);
    }

    return createCopy_(parent, source, nullptr);
}

Element*
Element::createCopy(Element* parent, const Element* source, Element* nextSibling) {
    return createCopy_(parent, source, nextSibling);
}

core::StringId Element::getOrCreateId() const {
    if (id_ == core::StringId()) {
        // create a new id !
        const ElementSpec* elementSpec = schema().findElementSpec(tagName_);
        core::StringId prefix = tagName_;
        if (elementSpec && elementSpec->defaultIdPrefix() != core::StringId()) {
            prefix = elementSpec->defaultIdPrefix();
        }
        Document* doc = document();
        for (Int i = 0; i < core::IntMax; ++i) {
            core::StringId id = core::StringId(core::format("{}{}", prefix, i));
            if (!doc->elementFromId(id)) {
                Element* ncThis = const_cast<Element*>(this);
                // This also registers id to the elementByIdMap.
                ncThis->setAttribute(strings::id, id);
                break;
            }
        }
    }
    return id_;
}

const Value& Element::getAuthoredAttribute(core::StringId name) const {
    if (const AuthoredAttribute* authored = findAuthoredAttribute_(name)) {
        return authored->value();
    }
    return Value::invalid();
}

const Value& Element::getAttribute(core::StringId name) const {
    if (const AuthoredAttribute* authored = findAuthoredAttribute_(name)) {
        return authored->value();
    }
    else {
        const ElementSpec* elementSpec = schema().findElementSpec(tagName_);
        const AttributeSpec* attributeSpec = elementSpec->findAttributeSpec(name);

        if (attributeSpec) {
            return attributeSpec->defaultValue();
        }
        else {
            return Value::invalid();
        }
    }
}

std::optional<Element*> Element::getElementFromPathAttribute(
    core::StringId name,
    core::StringId tagNameFilter) const {

    const Value& value = getAttribute(name);
    const Path* pathPtr = nullptr;

    if (const NoneOr<Path>* noneOrPath = value.getIf<NoneOr<Path>>()) {
        if (!noneOrPath->has_value()) {
            return std::nullopt;
        }
        pathPtr = &noneOrPath->value();
    }
    else {
        // cast as path or throw if attribute is not a path
        pathPtr = &value.get<Path>();
    }

    // resolve path (relative to this element if the path is relative
    dom::Element* element = getElementFromPath(*pathPtr);

    if (!element) {
        VGC_WARNING(
            LogVgcDom,
            "Path in attribute `{}` of element `{}` could not be resolved ({}).",
            name,
            tagName(),
            *pathPtr);
        return nullptr;
    }

    if (!tagNameFilter.isEmpty() && element->tagName() != tagNameFilter) {
        VGC_WARNING(
            LogVgcDom,
            "Path in attribute `{}` of element `{}` resolved to an element `{}` but `{}` "
            "was expected.",
            name,
            tagName(),
            element->tagName(),
            tagNameFilter);
        return nullptr;
    }

    return element;
}

void Element::setAttribute(core::StringId name, Value value) {
    core::History::do_<SetAttributeOperation>(
        document()->history(), this, name, std::move(value));
}

void Element::clearAttribute(core::StringId name) {
    if (AuthoredAttribute* authored = findAuthoredAttribute_(name)) {
        Int index = std::distance(&authoredAttributes_[0], authored);
        core::History::do_<RemoveAuthoredAttributeOperation>(
            document()->history(), this, name, index);
    }
}

AuthoredAttribute* Element::findAuthoredAttribute_(core::StringId name) {
    return authoredAttributes_.search(
        [name](const AuthoredAttribute& attr) { return attr.name() == name; });
}

const AuthoredAttribute* Element::findAuthoredAttribute_(core::StringId name) const {
    return const_cast<Element*>(this)->findAuthoredAttribute_(name);
}

void Element::onAttributeChanged_(
    core::StringId name,
    const Value& oldValue,
    const Value& newValue) {

    if (name == strings::name) {
        name_ = newValue.hasValue() ? newValue.get<core::StringId>() : core::StringId();
        document()->onElementNameChanged_(this);
    }
    else if (name == strings::id) {
        // TODO: deal with conflicts
        core::StringId oldId = id_;
        id_ = newValue.hasValue() ? newValue.get<core::StringId>() : core::StringId();
        document()->onElementIdChanged_(this, oldId);
    }
    attributeChanged().emit(name, oldValue, newValue);
}

void Element::prepareInternalPathsForUpdate_() const {
    for (const AuthoredAttribute& attr : authoredAttributes_) {
        const Value& value = attr.value();
        value.visitPaths([owner = this](const Path& path) {
            detail::PathUpdater::preparePathForUpdate(path, owner);
        });
    }
}

void Element::updateInternalPaths_(const detail::PathUpdateData& data) {
    for (AuthoredAttribute& attr : authoredAttributes_) {
        Value& value = const_cast<Value&>(attr.value());
        value.visitPaths([owner = this, &data](Path& path) {
            detail::PathUpdater::updatePath(path, owner, data);
        });
    }
}

} // namespace vgc::dom
