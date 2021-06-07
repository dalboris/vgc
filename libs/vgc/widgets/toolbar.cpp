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

// Implementation Notes
// --------------------
//
// This is basically like a complex QSplitter allowing you to split and resize
// in both directions. See the following for inspiration on how to implement
// missing features:
//
// https://github.com/qt/qtbase/blob/5.12/src/widgets/widgets/qsplitter.cpp
//

#include <vgc/widgets/toolbar.h>

#include <QLineEdit>

#include <vgc/ui/button.h>
#include <vgc/ui/column.h>
#include <vgc/ui/label.h>
#include <vgc/ui/lineedit.h>
#include <vgc/ui/row.h>

#include <vgc/core/format.h>

namespace vgc {
namespace widgets {

Toolbar::Toolbar(QWidget* parent) :
    QToolBar(parent)
{
    int toolbarWidth = 150;
    int iconWidth = 64;
    int margin = 15;
    QSize iconSize(iconWidth, iconWidth);

    setOrientation(Qt::Vertical);
    setMovable(false);
    setIconSize(iconSize);

    QWidget* topMargin = new QWidget();
    topMargin->setMinimumSize(toolbarWidth, margin);
    topMargin->setStyleSheet("background-color: none");
    addWidget(topMargin);

    QWidget* lineEdit = new QLineEdit();
    addWidget(lineEdit);

    colorToolButton_ = new ColorToolButton();
    colorToolButton_->setToolTip(tr("Current color (C)"));
    colorToolButton_->setStatusTip(tr("Click to open the color selector"));
    colorToolButton_->setIconSize(iconSize);
    colorToolButton_->setMinimumSize(toolbarWidth, iconWidth);
    colorToolButton_->updateIcon();

    colorToolButton_ = new ColorToolButton();
    colorToolButton_->setToolTip(tr("Current color (C)"));
    colorToolButton_->setStatusTip(tr("Click to open the color selector"));
    colorToolButton_->setIconSize(iconSize);
    colorToolButton_->setMinimumSize(toolbarWidth, iconWidth);
    colorToolButton_->updateIcon();

    colorToolButtonAction_ = addWidget(colorToolButton_);
    colorToolButtonAction_->setText(tr("Color"));
    colorToolButtonAction_->setToolTip(tr("Color (C)"));
    colorToolButtonAction_->setStatusTip(tr("Click to open the color selector"));
    colorToolButtonAction_->setShortcut(QKeySequence(Qt::Key_C));
    //colorToolButtonAction_->setShortcutContext(Qt::ApplicationShortcut);

    ui::RowPtr row = ui::Row::create();
    row->createChild<ui::Button>("Button 1");
    row->createChild<ui::LineEdit>("LineEdit With Long Text");
    addWidget(new UiWidget(row, this));

    auto colorPalettePtr = ui::ColorPalette::create();
    colorPalette_ = colorPalettePtr.get();
    colorPaletteq_ = new UiWidget(colorPalettePtr, this);
    addWidget(colorPaletteq_);

    ui::RowPtr row2 = ui::Row::create();
    row2->createChild<ui::Button>("Button 3");
    row2->createChild<ui::Button>("Button 4");
    addWidget(new UiWidget(row2, this));

    /*
    auto labelPtr = ui::Label::create("Hello!");
    //auto labelPtr = ui::Label::create("Ленивый рыжий кот");
    //auto labelPtr = ui::Label::create("كسول الزنجبيل القط");
    //auto labelPtr = ui::Label::create("ff");
    auto* labelq = new UiWidget(labelPtr, this);
    labelq->setMinimumSize(toolbarWidth, 40);
    addWidget(labelq);
    */

    connect(colorToolButtonAction_, SIGNAL(triggered()), colorToolButton_, SLOT(click()));
    connect(colorToolButton_, &ColorToolButton::colorChanged, this, &Toolbar::onColorToolButtonColorChanged_);
    colorPalette_->colorSelected.connect(std::bind(&Toolbar::onColorPaletteColorSelected_, this));
}

Toolbar::~Toolbar()
{

}

core::Color Toolbar::color() const
{
    return colorPalette_->selectedColor();
}

void Toolbar::onColorToolButtonColorChanged_()
{
    colorPalette_->setSelectedColor(colorToolButton_->color());
    Q_EMIT colorChanged(color());
    // Note: setSelectedColor does not emit colorSelected.
}

void Toolbar::onColorPaletteColorSelected_()
{
    colorToolButton_->setColor(colorPalette_->selectedColor());
    // Note: setColor emits colorChanged()
}

} // namespace widgets
} // namespace vgc
