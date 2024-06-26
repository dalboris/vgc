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

#ifndef VGC_UI_TABBAR_H
#define VGC_UI_TABBAR_H

#include <vgc/ui/flex.h>
#include <vgc/ui/label.h>
#include <vgc/ui/widget.h>

namespace vgc::ui {

VGC_DECLARE_OBJECT(TabBar);
VGC_DECLARE_OBJECT(TabBody);

namespace detail {

struct TabSpec {
    bool isClosable = false;
};

} // namespace detail

/// \class vgc::ui::TabBar
/// \brief A bar showing different tabs.
///
class VGC_UI_API TabBar : public Widget {
private:
    VGC_OBJECT(TabBar, Widget)

protected:
    TabBar(CreateKey);

public:
    /// Creates a `TabBar`.
    ///
    static TabBarPtr create();

    /// Adds a new tab to this tab bar with the given `label`.
    ///
    /// If `isClosable` is true, then a close icon is added to allow users to
    /// close the tab, that is, remove it from this tab bar.
    ///
    /// \sa `tabClosed()`.
    ///
    void addTab(std::string_view label, bool isClosable = true);

    /// Returns the number of tabs in this tab bar.
    ///
    Int numTabs() const;

    /// This signal is emitted whenever a tab is closed.
    ///
    VGC_SIGNAL(tabClosed, (Int, tabIndex))

protected:
    void onMouseEnter() override;
    void onMouseLeave() override;
    geometry::Vec2f computePreferredSize() const override;
    void updateChildrenGeometry() override;

private:
    core::Array<detail::TabSpec> tabSpecs_;

    WidgetPtr tabs_;
    WidgetPtr close_;

    void onCloseTab_();
    VGC_SLOT(onCloseTab_)
};

} // namespace vgc::ui

#endif // VGC_UI_TABBAR_H
