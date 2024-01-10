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

#ifndef VGC_TOOLS_ARRANGE_H
#define VGC_TOOLS_ARRANGE_H

#include <vgc/tools/api.h>
#include <vgc/ui/command.h>
#include <vgc/ui/module.h>

VGC_DECLARE_OBJECT(vgc::canvas, CanvasManager);

namespace vgc::tools {

namespace commands {

VGC_TOOLS_API
VGC_UI_DECLARE_COMMAND(bringForward)

VGC_TOOLS_API
VGC_UI_DECLARE_COMMAND(bringBackward)

} // namespace commands

VGC_DECLARE_OBJECT(ArrangeModule);

/// \class vgc::tools::ArrangeModule
/// \brief A module to import all arrange-related actions (bring forward, etc.).
///
class VGC_TOOLS_API ArrangeModule : public ui::Module {
private:
    VGC_OBJECT(ArrangeModule, ui::Module)

protected:
    ArrangeModule(CreateKey, const ui::ModuleContext& context);

public:
    /// Creates the `ArrangeModule` module.
    ///
    static ArrangeModulePtr create(const ui::ModuleContext& context);

private:
    canvas::CanvasManagerWeakPtr canvasManager_;

    void onBringForward_();
    VGC_SLOT(onBringForward_)

    void onBringBackward_();
    VGC_SLOT(onBringBackward_)
};

} // namespace vgc::tools

#endif // VGC_TOOLS_ARRANGE_H
