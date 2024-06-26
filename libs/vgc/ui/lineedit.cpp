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

#include <vgc/ui/lineedit.h>

#include <QClipboard>
#include <QGuiApplication>

#include <vgc/core/array.h>
#include <vgc/core/color.h>
#include <vgc/core/colors.h>
#include <vgc/style/strings.h>
#include <vgc/style/types.h>
#include <vgc/ui/cursor.h>
#include <vgc/ui/genericcommands.h>
#include <vgc/ui/preferredsizecalculator.h>
#include <vgc/ui/strings.h>

#include <vgc/ui/detail/paintutil.h>

namespace vgc::ui {

namespace {

namespace commands_ {

VGC_UI_DEFINE_TRIGGER_COMMAND( //
    cut,
    "ui.lineedit.cut",
    "Cut",
    Shortcut(),
    "",
    ui::commands::generic::cut());

VGC_UI_DEFINE_TRIGGER_COMMAND( //
    copy,
    "ui.lineedit.copy",
    "Copy",
    Shortcut(),
    "",
    ui::commands::generic::copy());

VGC_UI_DEFINE_TRIGGER_COMMAND( //
    paste,
    "ui.lineedit.paste",
    "Paste",
    Shortcut(),
    "",
    ui::commands::generic::paste());

} // namespace commands_

void copyToClipboard_(
    std::string_view text,
    QClipboard::Mode mode = QClipboard::Clipboard) {

    QClipboard* clipboard = QGuiApplication::clipboard();
    size_t size = core::clamp(text.size(), 0, core::tmax<int>);
    QString qtext = QString::fromUtf8(text.data(), static_cast<int>(size));
    clipboard->setText(qtext, mode);
}

void copyToX11SelectionClipboard_(graphics::RichText* richText) {
    static bool supportsSelection = QGuiApplication::clipboard()->supportsSelection();
    if (supportsSelection && richText->hasSelection()) {
        copyToClipboard_(richText->selectedTextView(), QClipboard::Selection);
    }
}

} // namespace

LineEdit::LineEdit(CreateKey key, std::string_view text)
    : Widget(key)
    , richText_(graphics::RichText::create()) {

    setFocusPolicy(FocusPolicy::Click | FocusPolicy::Tab);
    setTextInputReceiver(true);
    addStyleClass(strings::LineEdit);
    appendChildStylableObject(richText_.get());
    setText(text);

    // XXX: Implement a mechanism to share actions between widgets, instead of
    // having each LineEdit store its own copy of all LineEdit actions?
    //
    // Example: `virtual ActionListView Widget::classActions() const;`

    defineAction(commands_::cut(), onCutSlot_());
    defineAction(commands_::copy(), onCopySlot_());
    defineAction(commands_::paste(), onPasteSlot_());
}

LineEditPtr LineEdit::create() {
    return core::createObject<LineEdit>("");
}

LineEditPtr LineEdit::create(std::string_view text) {
    return core::createObject<LineEdit>(text);
}

void LineEdit::setText(std::string_view text) {
    if (text != richText_->text()) {
        richText_->setText(text);
        onTextChanged_();
    }
}

void LineEdit::moveCursor(graphics::RichTextMoveOperation operation, bool select) {
    richText_->moveCursor(operation, select);
    onCursorMoved_(select);
}

void LineEdit::onResize() {
    SuperClass::onResize();
    richText_->setRect(contentRect());
    reload_ = true;
}

void LineEdit::onPaintCreate(graphics::Engine* engine) {
    SuperClass::onPaintCreate(engine);
    triangles_ = engine->createTriangleList(graphics::BuiltinGeometryLayout::XYRGB);
}

void LineEdit::onPaintDraw(graphics::Engine* engine, PaintOptions options) {

    SuperClass::onPaintDraw(engine, options);

    if (reload_) {
        reload_ = false;
        core::FloatArray a;

        // Draw text
        richText_->fill(a);

        // Load triangles data
        engine->updateVertexBufferData(triangles_, std::move(a));
    }
    engine->setProgram(graphics::BuiltinProgram::Simple);
    engine->draw(triangles_);
}

void LineEdit::onPaintDestroy(graphics::Engine* engine) {
    SuperClass::onPaintDestroy(engine);
    triangles_.reset();
}

void LineEdit::extendSelection_(const geometry::Vec2f& point) {
    Int position = richText_->positionFromPoint(point, mouseSelectionMarkers_);
    Int beginPosition;
    Int endPosition;
    if (position < mouseSelectionInitialPair_.first) {
        beginPosition = mouseSelectionInitialPair_.second;
        endPosition = position;
    }
    else if (position < mouseSelectionInitialPair_.second) {
        beginPosition = mouseSelectionInitialPair_.first;
        endPosition = mouseSelectionInitialPair_.second;
    }
    else {
        beginPosition = mouseSelectionInitialPair_.first;
        endPosition = position;
    }
    richText_->setSelectionStart(beginPosition);
    richText_->setSelectionEnd(endPosition);
}

void LineEdit::resetSelectionInitialPair_() {
    Int position = richText_->selectionStart();
    mouseSelectionMarkers_ = graphics::TextBoundaryMarker::Grapheme;
    mouseSelectionInitialPair_ = std::make_pair(position, position);
}

bool LineEdit::onMouseMove(MouseMoveEvent* event) {
    if (mouseButton_ == MouseButton::Left) {
        geometry::Vec2f mousePosition = event->position();
        geometry::Vec2f mouseOffset = richText_->rect().pMin();
        geometry::Vec2f point = mousePosition - mouseOffset;
        extendSelection_(point);
        requestRepaint_();
    }
    return true;
}

bool LineEdit::onMousePress(MousePressEvent* event) {
    // Only support one mouse button at a time
    if (mouseButton_ != MouseButton::None) {
        return false;
    }
    mouseButton_ = event->button();

    const bool left = mouseButton_ == MouseButton::Left;
    const bool right = mouseButton_ == MouseButton::Right;
    const bool middle = mouseButton_ == MouseButton::Middle;
    const bool shift = event->modifierKeys().has(ModifierKey::Shift);

    // Handle double/triple left click
    geometry::Vec2f mousePosition = event->position();
    if (left) {
        if (numLeftMouseButtonClicks_ > 0
            && leftMouseButtonStopwatch_.elapsedMilliseconds() < 500
            && (mousePosition - mousePositionOnPress_).length() < 5) {

            ++numLeftMouseButtonClicks_;
        }
        else {
            numLeftMouseButtonClicks_ = 1;
        }
        leftMouseButtonStopwatch_.restart();
    }
    else {
        numLeftMouseButtonClicks_ = 0;
    }
    mousePositionOnPress_ = mousePosition;

    // Change cursor position on press of any of the 3 standard mouse buttons
    if (left || right || middle) {
        geometry::Vec2f mouseOffset = richText_->rect().pMin();
        geometry::Vec2f point = mousePosition - mouseOffset;
        if (left && shift) {
            extendSelection_(point);
        }
        else {
            // On multiple left clicks, cycle between set cursor / select word / select line
            Int mod = numLeftMouseButtonClicks_ % 3;
            if (numLeftMouseButtonClicks_ < 2 || mod == 1) {
                mouseSelectionMarkers_ = graphics::TextBoundaryMarker::Grapheme;
                Int position =
                    richText_->positionFromPoint(point, mouseSelectionMarkers_);
                mouseSelectionInitialPair_ = {position, position};
            }
            else {
                mouseSelectionMarkers_ =
                    mod ? graphics::TextBoundaryMarker::Word
                        : graphics::TextBoundaryMarker::MandatoryLineBreak;
                mouseSelectionInitialPair_ =
                    richText_->positionPairFromPoint(point, mouseSelectionMarkers_);
            }
            richText_->setSelectionStart(mouseSelectionInitialPair_.first);
            richText_->setSelectionEnd(mouseSelectionInitialPair_.second);
        }

        // Middle-button paste on supported platforms (e.g., X11)
        if (middle) {
            QClipboard* clipboard = QGuiApplication::clipboard();
            std::string t = clipboard->text(QClipboard::Selection).toStdString();
            richText_->insertText(t);
            resetSelectionInitialPair_();
            textChanged().emit();
            textEdited().emit();
        }
    }

    requestRepaint_();
    return true;
}

bool LineEdit::onMouseRelease(MouseReleaseEvent* event) {
    // Only support one mouse button at a time
    if (mouseButton_ != event->button()) {
        return false;
    }

    if (mouseButton_ == MouseButton::Left) {
        copyToX11SelectionClipboard_(richText_.get());
    }

    mouseButton_ = MouseButton::None;
    return true;
}

void LineEdit::onMouseEnter() {
    cursorChanger_.set(Qt::IBeamCursor);
}

void LineEdit::onMouseLeave() {
    cursorChanger_.clear();
}

void LineEdit::onFocusIn(FocusReason) {
    richText_->setCursorVisible(true);
    requestRepaint_();
}

void LineEdit::onFocusOut(FocusReason) {
    richText_->setCursorVisible(false);
    requestRepaint_();
}

void LineEdit::onFocusStackIn(FocusReason) {
    oldText_ = text();
    richText_->setSelectionVisible(true);
}

void LineEdit::onFocusStackOut(FocusReason) {
    richText_->setSelectionVisible(false);
    richText_->clearSelection();
    requestRepaint_();
    editingFinished().emit();
}

bool LineEdit::onKeyPress(KeyPressEvent* event) {

    using Op = graphics::RichTextMoveOperation;

    const Key key = event->key();
    const bool ctrl = event->modifierKeys().has(ModifierKey::Ctrl);
    const bool shift = event->modifierKeys().has(ModifierKey::Shift);

    bool handled = true;
    bool isMoveOperation = false;

    if (key == Key::Delete || key == Key::Backspace) {
        if (key == Key::Delete) {
            richText_->deleteFromCursor(ctrl ? Op::NextWord : Op::NextCharacter);
        }
        else { // backspace
            richText_->deleteFromCursor(ctrl ? Op::PreviousWord : Op::PreviousCharacter);
        }
    }
    else if (key == Key::Home) {
        richText_->moveCursor(ctrl ? Op::StartOfText : Op::StartOfLine, shift);
        isMoveOperation = true;
    }
    else if (key == Key::End) {
        richText_->moveCursor(ctrl ? Op::EndOfText : Op::EndOfLine, shift);
        isMoveOperation = true;
    }
    else if (key == Key::Left) {
        if (richText_->hasSelection() && !shift) {
            richText_->moveCursor(Op::LeftOfSelection);
        }
        else {
            richText_->moveCursor(ctrl ? Op::LeftOneWord : Op::LeftOneCharacter, shift);
        }
        isMoveOperation = true;
    }
    else if (key == Key::Right) {
        if (richText_->hasSelection() && !shift) {
            richText_->moveCursor(Op::RightOfSelection);
        }
        else {
            richText_->moveCursor(ctrl ? Op::RightOneWord : Op::RightOneCharacter, shift);
        }
        isMoveOperation = true;
    }
    else if (ctrl && key == Key::A) {
        richText_->selectAll();
    }
    else if (key == Key::Escape) {
        setText(oldText_);
        clearFocus(FocusReason::Other);
    }
    else if (event->key() == Key::Enter || event->key() == Key::Return) {
        clearFocus(FocusReason::Other);
    }
    else if (key == Key::Tab) {
        handled = false;
    }
    else if (!ctrl) {
        const std::string& t = event->text();
        bool isControlCharacter = (t.size() == 1) && (t[0] >= 0) && (t[0] < 32);
        if (!t.empty() && !isControlCharacter) {
            richText_->insertText(t);
        }
        else {
            handled = false;
        }
    }
    else {
        handled = false;
    }

    if (handled) {
        if (isMoveOperation) {
            onCursorMoved_(shift);
        }
        else {
            onTextEdited_();
        }
    }

    return handled;
}

geometry::Vec2f LineEdit::computePreferredSize() const {
    PreferredSizeCalculator calc(this);
    calc.add(richText_->preferredSize());
    calc.addPaddingAndBorder();
    return calc.compute();
}

void LineEdit::requestRepaint_() {
    reload_ = true;
    requestRepaint();
}

// Note: this shouldn't be called for mouse actions, since these implement
// their own handling of the selection pair and copying to the clipboard.
//
void LineEdit::onCursorMoved_(bool select) {
    if (select) {
        copyToX11SelectionClipboard_(richText_.get());
    }
    resetSelectionInitialPair_();
    requestRepaint_();
}

void LineEdit::onTextChanged_() {
    resetSelectionInitialPair_();
    requestRepaint_();
    textChanged().emit();
}

void LineEdit::onTextEdited_() {
    onTextChanged_();
    textEdited().emit();
}

void LineEdit::onCut_() {
    if (!richText_->hasSelection()) {
        return;
    }
    copyToClipboard_(richText_->selectedTextView());
    richText_->deleteSelectedText();
    onTextEdited_();
}

void LineEdit::onCopy_() {
    if (!richText_->hasSelection()) {
        return;
    }
    copyToClipboard_(richText_->selectedTextView());
}

void LineEdit::onPaste_() {
    QClipboard* clipboard = QGuiApplication::clipboard();
    std::string t = clipboard->text().toStdString();
    richText_->insertText(t);
    onTextEdited_();
}

} // namespace vgc::ui
