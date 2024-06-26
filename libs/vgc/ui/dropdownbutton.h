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

#ifndef VGC_UI_DROPDOWNBUTTON_H
#define VGC_UI_DROPDOWNBUTTON_H

#include <vgc/ui/button.h>
#include <vgc/ui/iconwidget.h>

namespace vgc::ui {

VGC_DECLARE_OBJECT(Menu);
VGC_DECLARE_OBJECT(DropdownButton);

/// \enum vgc::ui::DropDirection
/// \brief The direction in which a dropdown overlay should appear.
///
enum class DropDirection {
    Vertical,
    Horizontal,
};

/// \class vgc::ui::DropdownButton
/// \brief A button with the ability to open a dropdown overlay.
///
class VGC_UI_API DropdownButton : public Button {
private:
    VGC_OBJECT(DropdownButton, Button)
    friend Menu;

protected:
    DropdownButton(CreateKey, Action* action, FlexDirection layoutDirection);

public:
    /// Creates an DropdownButton with the given `action`.
    ///
    static DropdownButtonPtr
    create(Action* action, FlexDirection layoutDirection = FlexDirection::Column);

    /// Returns the `DropDirection` of this dropdown button.
    ///
    DropDirection dropDirection() const {
        return dropDirection_;
    }

    /// Sets the `DropDirection` of this dropdown button.
    ///
    void setDropDirection(DropDirection direction);

    /// Returns whether the arrow is visible.
    ///
    bool isArrowVisible() const;

    /// Sets whether the arrow is visible. By default, it is visible.
    ///
    void setArrowVisible(bool visible);

    Menu* popupMenu() const {
        return popupMenu_;
    };

    void closePopupMenu();

    VGC_SIGNAL(menuPopupOpened);
    VGC_SIGNAL(menuPopupClosed, (bool, recursive));

private:
    DropDirection dropDirection_ = DropDirection::Vertical;
    Menu* popupMenu_ = nullptr;

    IconWidgetWeakPtr arrowIcon_;
    void updateArrowIcon_();

    // The menu calls this when it opens as a popup.
    void onMenuPopupOpened_(Menu* menu);

    void onMenuPopupClosed_(bool recursive);
    VGC_SLOT(onMenuPopupClosedSlot_, onMenuPopupClosed_);
};

} // namespace vgc::ui

#endif // VGC_UI_DROPDOWNBUTTON_H
