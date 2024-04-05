// Copyright 2024 The VGC Developers
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

#include <vgc/canvas/experimental.h>

#include <vgc/ui/boolsettingedit.h>
#include <vgc/ui/column.h>
#include <vgc/ui/panelcontext.h>
#include <vgc/ui/strings.h>

namespace vgc::canvas {

namespace {

core::StringId s_with_padding("with-padding");
core::StringId s_experimental("experimental");

} // namespace

namespace experimental {

ui::BoolSetting& saveInputSketchPoints() {
    static ui::BoolSettingSharedPtr setting = ui::BoolSetting::create(
        ui::settings::session(),
        "canvas.experimental.saveInputSketchPoints",
        "Save Input Sketch Points",
        false);
    return *setting;
}

ui::BoolSetting& showInputSketchPoints() {
    static ui::BoolSettingSharedPtr setting = ui::BoolSetting::create(
        ui::settings::session(),
        "canvas.experimental.showInputSketchPoints",
        "Show Input Sketch Points",
        false);
    return *setting;
}

} // namespace experimental

ExperimentalPanel::ExperimentalPanel(CreateKey key, const ui::PanelContext& context)
    : Panel(key, context, label) {

    ui::WidgetPtr w = createChild<ui::Column>();
    w->createChild<ui::BoolSettingEdit>(&experimental::saveInputSketchPoints());
    w->createChild<ui::BoolSettingEdit>(&experimental::showInputSketchPoints());

    addStyleClass(s_with_padding);
    addStyleClass(s_experimental);
    addStyleClass(ui::strings::settings);
}

ExperimentalPanelPtr ExperimentalPanel::create(const ui::PanelContext& context) {
    return core::createObject<ExperimentalPanel>(context);
}

} // namespace vgc::canvas