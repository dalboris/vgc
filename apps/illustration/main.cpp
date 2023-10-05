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

#include <vgc/app/canvasapplication.h>


// XXX temporary test
#include <QSurfaceFormat>
#include <QtDebug>
#include <vgc/ui/detail/qopenglengine.h>

int main(int argc, char* argv[]) {


    QSurfaceFormat format_;
    format_.setProfile(QSurfaceFormat::CoreProfile);
    format_.setVersion(vgc::ui::detail::qopengl::requiredOpenGLVersionMajor, vgc::ui::detail::qopengl::requiredOpenGLVersionMinor);
    //format_.setOption(QSurfaceFormat::DebugContext);

    // XXX only allow D24_S8 for now..
    format_.setDepthBufferSize(24);
    format_.setStencilBufferSize(8);
    format_.setSamples(32);
    format_.setSwapInterval(0);
    //PixelFormat pixelFormat = createInfo.windowSwapChainFormat().pixelFormat();
    //if (pixelFormat == PixelFormat::RGBA_8_UNORM_SRGB) {
    format_.setColorSpace(QSurfaceFormat::sRGBColorSpace);
    //}
    //else {
    //    format_.setColorSpace(QSurfaceFormat::DefaultColorSpace);
    //}

    // XXX use buffer count
    format_.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format_);

    QSurfaceFormat format =  QSurfaceFormat::defaultFormat();
    qDebug() << format;


    auto application =
        vgc::app::CanvasApplication::create(argc, argv, "VGC Illustration");
    application->setOrganizationName("VGC Software");
    application->setOrganizationDomain("vgc.io");
    application->setWindowIconFromResource("apps/illustration/icons/512.png");
    return application->exec();
}
