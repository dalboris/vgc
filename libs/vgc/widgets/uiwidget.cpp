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

#include <vgc/widgets/uiwidget.h>

#include <QMouseEvent>
#include <QInputMethodEvent>

#include <vgc/core/paths.h>
#include <vgc/geometry/camera2d.h>
#include <vgc/widgets/qtutil.h>

namespace vgc {
namespace widgets {

namespace {

// Returns the file path of a shader file as a QString
QString shaderPath_(const std::string& name) {
    std::string path = core::resourcePath("graphics/opengl/" + name);
    return toQt(path);
}

core::Mat4f toMat4f(const core::Mat4d& m) {
    // TODO: implement Mat4d to Mat4f conversion directly in Mat4x classes
    return core::Mat4f(
               (float)m(0,0), (float)m(0,1), (float)m(0,2), (float)m(0,3),
               (float)m(1,0), (float)m(1,1), (float)m(1,2), (float)m(1,3),
               (float)m(2,0), (float)m(2,1), (float)m(2,2), (float)m(2,3),
               (float)m(3,0), (float)m(3,1), (float)m(3,2), (float)m(3,3));
}

QMatrix4x4 toQtMatrix(const core::Mat4f& m) {
    return QMatrix4x4(
               m(0,0), m(0,1), m(0,2), m(0,3),
               m(1,0), m(1,1), m(1,2), m(1,3),
               m(2,0), m(2,1), m(2,2), m(2,3),
               m(3,0), m(3,1), m(3,2), m(3,3));
}

struct XYRGBVertex {
    float x, y, r, g, b;
};

} // namespace

UiWidget::UiWidget(ui::WidgetPtr widget, QWidget* parent) :
    QOpenGLWidget(parent),
    widget_(widget),
    engine_(UiWidgetEngine::create()),
    isInitialized_(false)
{
    setMouseTracking(true);
    widget_->repaintRequested.connect(std::bind(
        &UiWidget::onRepaintRequested, this));
    widget_->focusRequested.connect(std::bind(
        &UiWidget::onFocusRequested, this));

    // Handle dead keys and complex input methods.
    //
    // Also see:
    // - inputMethodQuery(Qt::InputMethodQuery)
    // - inputMethodEvent(QInputMethodEvent*)
    //
    // XXX Shouldn't we enable/disable this property dynamically at runtime,
    // based on which ui::Widget has the focus? Is it even possible? Indeed, we
    // probably want to prevent an IME to popup if the focused widget doesn't
    // accept text input.
    //
    setAttribute(Qt::WA_InputMethodEnabled, true);
}

UiWidget::~UiWidget()
{
    makeCurrent();
    cleanupGL();
    doneCurrent();
}

QSize UiWidget::sizeHint() const
{
    core::Vec2f s = widget_->preferredSize();
    return QSize(core::ifloor<int>(s[0]), core::ifloor<int>(s[1]));
}

namespace {

ui::MouseEventPtr convertEvent(QMouseEvent* event)
{
    const QPointF& p = event->localPos();
    return ui::MouseEvent::create(fromQtf(p));
}

} // namespace

void UiWidget::mouseMoveEvent(QMouseEvent *event)
{
    ui::MouseEventPtr e = convertEvent(event);
    event->setAccepted(widget_->onMouseMove(e.get()));
}

void UiWidget::mousePressEvent(QMouseEvent *event)
{
    ui::MouseEventPtr e = convertEvent(event);
    event->setAccepted(widget_->onMousePress(e.get()));
}

void UiWidget::mouseReleaseEvent(QMouseEvent *event)
{
    ui::MouseEventPtr e = convertEvent(event);
    event->setAccepted(widget_->onMouseRelease(e.get()));
}

void UiWidget::enterEvent(QEvent* event)
{
    event->setAccepted(widget_->onMouseEnter());
}

void UiWidget::leaveEvent(QEvent* event)
{
    event->setAccepted(widget_->onMouseLeave());
}

void UiWidget::focusInEvent(QFocusEvent* /*event*/)
{
    widget_->setTreeActive(true);
}

void UiWidget::focusOutEvent(QFocusEvent* /*event*/)
{
    widget_->setTreeActive(false);
}

void UiWidget::keyPressEvent(QKeyEvent* event)
{
    bool accepted = widget_->onKeyPress(event);
    event->setAccepted(accepted);
}

void UiWidget::keyReleaseEvent(QKeyEvent* event)
{
    event->setAccepted(widget_->onKeyRelease(event));
}

UiWidget::OpenGLFunctions* UiWidget::openGLFunctions() const
{
    return context()->versionFunctions<OpenGLFunctions>();
}

QVariant UiWidget::inputMethodQuery(Qt::InputMethodQuery) const
{
    // This function allows the input method editor (commonly abbreviated IME)
    // to query useful info about the widget state that it needs to operate.
    // For more info on IME in general, see:
    //
    // https://en.wikipedia.org/wiki/Input_method
    //
    // For inspiration on how to implement this function, see QLineEdit:
    //
    // https://github.com/qt/qtbase/blob/ec7ff5cede92412b929ff30207b0eeafce93ee3b/src/widgets/widgets/qlineedit.cpp#L1849
    //
    // For now, we simply return an empty QVariant. Most likely, this means
    // that many (most?) IME won't work with our app. Fixing this is left for
    // future work.
    //
    // Also see:
    //
    // - https://stackoverflow.com/questions/43078567/qt-inputmethodevent-get-the-keyboard-key-that-was-pressed
    // - https://stackoverflow.com/questions/3287180/putting-ime-in-a-custom-text-box-derived-from-control
    // - https://stackoverflow.com/questions/434048/how-do-you-use-ime
    //
    return QVariant();
}

void UiWidget::inputMethodEvent(QInputMethodEvent* event)
{
    // For now, we only use a very simple implementation that, at the very
    // least, correctly handle dead keys. See:
    //
    // https://stackoverflow.com/questions/28793356/qt-and-dead-keys-in-a-custom-widget
    //
    // Most likely, complex IME still won't work correctly, see comment in the
    // implementation of inputMethodQuery().
    //
    if (!event->commitString().isEmpty()) {
        // XXX Shouldn't we pass more appropriate modifiers?
        QKeyEvent keyEvent(QEvent::KeyPress, 0, Qt::NoModifier, event->commitString());
        keyPressEvent(&keyEvent);
    }
}

bool UiWidget::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        e->accept();
    }
    return QWidget::event(e);
}

void UiWidget::initializeGL()
{
    // Initialize shader program
    shaderProgram_.addShaderFromSourceFile(QOpenGLShader::Vertex, shaderPath_("iv4pos_iv4col_um4proj_um4view_ov4fcol.v.glsl"));
    shaderProgram_.addShaderFromSourceFile(QOpenGLShader::Fragment, shaderPath_("iv4fcol.f.glsl"));
    shaderProgram_.link();

    // Get shader locations
    shaderProgram_.bind();
    posLoc_  = shaderProgram_.attributeLocation("pos");
    colLoc_  = shaderProgram_.attributeLocation("col");
    projLoc_ = shaderProgram_.uniformLocation("proj");
    viewLoc_ = shaderProgram_.uniformLocation("view");
    shaderProgram_.release();

    // Initialize engine
    OpenGLFunctions* f = openGLFunctions();
    engine_->initialize(f, &shaderProgram_, posLoc_, colLoc_, projLoc_, viewLoc_);

    // Initialize widget for painting.
    // Note that initializedGL() is never called if the widget is never visible.
    // Therefore it's important to keep track whether it has been called, so that
    // we don't call onPaintDestroy() without first calling onPaintCreate()
    isInitialized_ = true;
    widget_->onPaintCreate(engine_.get());
}

void UiWidget::resizeGL(int w, int h)
{
    geometry::Camera2d c;
    c.setViewportSize(w, h);
    proj_ = toMat4f(c.projectionMatrix());

    // Set new widget geometry. Note: if w or h is > 16777216 (=2^24), then static_cast
    // silently rounds to the nearest integer representable as a float. See:
    //   https://stackoverflow.com/a/60339495/1951907
    // Should we issue a warning in these cases?
    widget()->setGeometry(0, 0, static_cast<float>(w), static_cast<float>(h));

    // Note: paintGL will automatically be called after this
}

void UiWidget::paintGL()
{
    shaderProgram_.bind();
    engine_->setProjectionMatrix(proj_);
    engine_->setViewMatrix(core::Mat4f::identity());
    engine_->clear(core::Color(0.337, 0.345, 0.353));
    widget_->onPaintDraw(engine_.get());
    shaderProgram_.release();
}

void UiWidget::cleanupGL()
{
    if (isInitialized_) {
        widget_->onPaintDestroy(engine_.get());
        isInitialized_ = false;
    }
}

void UiWidget::onRepaintRequested()
{
    update();
}

void UiWidget::onFocusRequested()
{
    setFocus();
    // Note: Under the hood, this calls setFocus(Qt::OtherFocusReason)
}

UiWidgetEngine::UiWidgetEngine() :
    graphics::Engine()
{
    projectionMatrices_.clear();
    projectionMatrices_.append(core::Mat4f::identity());
    viewMatrices_.clear();
    viewMatrices_.append(core::Mat4f::identity());
}

/* static */
UiWidgetEnginePtr UiWidgetEngine::create()
{
    return UiWidgetEnginePtr(new UiWidgetEngine());
}

void UiWidgetEngine::initialize(UiWidget::OpenGLFunctions* functions,
                                QOpenGLShaderProgram* shaderProgram,
                                int posLoc, int colLoc, int projLoc, int viewLoc)
{
    openGLFunctions_ = functions;
    shaderProgram_ = shaderProgram;
    posLoc_ = posLoc;
    colLoc_ = colLoc;
    projLoc_ = projLoc;
    viewLoc_ = viewLoc;
}

void UiWidgetEngine::clear(const core::Color& color)
{
    openGLFunctions_->glClearColor(
        static_cast<float>(color.r()),
        static_cast<float>(color.g()),
        static_cast<float>(color.b()),
        static_cast<float>(color.a()));
    openGLFunctions_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

core::Mat4f UiWidgetEngine::projectionMatrix()
{
    return projectionMatrices_.last();
}

void UiWidgetEngine::setProjectionMatrix(const core::Mat4f& m)
{
    projectionMatrices_.last() = m;
    shaderProgram_->setUniformValue(projLoc_, toQtMatrix(m));
}

void UiWidgetEngine::pushProjectionMatrix()
{
    // Note: we need a temporary copy by value, since the address
    // of the current projection matrix might change during "append" in
    // case of memory re-allocation.
    core::Mat4f m = projectionMatrix();
    projectionMatrices_.append(m);
}

void UiWidgetEngine::popProjectionMatrix()
{
    projectionMatrices_.removeLast();
    shaderProgram_->setUniformValue(projLoc_, toQtMatrix(projectionMatrices_.last()));
}

core::Mat4f UiWidgetEngine::viewMatrix()
{
    return viewMatrices_.last();
}

void UiWidgetEngine::setViewMatrix(const core::Mat4f& m)
{
    viewMatrices_.last() = m;
    shaderProgram_->setUniformValue(viewLoc_, toQtMatrix(m));
}

void UiWidgetEngine::pushViewMatrix()
{
    // Note: we need a temporary copy by value, since the address
    // of the current view matrix might change during "append" in
    // case of memory re-allocation.
    core::Mat4f m = viewMatrix();
    viewMatrices_.append(m);
}

void UiWidgetEngine::popViewMatrix()
{
    viewMatrices_.removeLast();
    shaderProgram_->setUniformValue(viewLoc_, toQtMatrix(viewMatrices_.last()));
}

Int UiWidgetEngine::createTriangles()
{
    // Get or create TrianglesBuffer
    Int id = trianglesIdGenerator_.generate();
    if (id >= trianglesBuffers_.length()) {
        trianglesBuffers_.append(TrianglesBuffer()); // use Array growth policy
    }
    if (id >= trianglesBuffers_.length()) {
        trianglesBuffers_.resize(id+1);              // bypass Array growth policy
    }
    TrianglesBuffer& r = trianglesBuffers_[id];

    // Create VBO/VAO for rendering triangles
    r.vboTriangles.create();
    r.vaoTriangles = new QOpenGLVertexArrayObject();
    r.vaoTriangles->create();
    GLsizei stride  = sizeof(XYRGBVertex);
    GLvoid* posPointer = reinterpret_cast<void*>(offsetof(XYRGBVertex, x));
    GLvoid* colPointer = reinterpret_cast<void*>(offsetof(XYRGBVertex, r));
    GLboolean normalized = GL_FALSE;
    r.vaoTriangles->bind();
    r.vboTriangles.bind();
    openGLFunctions_->glEnableVertexAttribArray(posLoc_);
    openGLFunctions_->glEnableVertexAttribArray(colLoc_);
    openGLFunctions_->glVertexAttribPointer(posLoc_, 2, GL_FLOAT, normalized, stride, posPointer);
    openGLFunctions_->glVertexAttribPointer(colLoc_, 3, GL_FLOAT, normalized, stride, colPointer);
    r.vboTriangles.release();
    r.vaoTriangles->release();

    return id;
}

void UiWidgetEngine::loadTriangles(Int id, const float* data, Int length)
{
    if (length < 0) {
        throw core::NegativeIntegerError(core::format(
            "Negative length ({}) provided to loadTriangles()", length));
    }
    TrianglesBuffer& r = trianglesBuffers_[id];
    r.numVertices = length / 5;
    r.vboTriangles.bind();
    r.vboTriangles.allocate(data, r.numVertices * sizeof(XYRGBVertex));
    r.vboTriangles.release();
}

void UiWidgetEngine::drawTriangles(Int id)
{
    TrianglesBuffer& r = trianglesBuffers_[id];
    openGLFunctions_->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    r.vaoTriangles->bind();
    openGLFunctions_->glDrawArrays(GL_TRIANGLES, 0, r.numVertices);
    r.vaoTriangles->release();
}

void UiWidgetEngine::destroyTriangles(Int id)
{
    TrianglesBuffer& r = trianglesBuffers_[id];
    r.vaoTriangles->destroy();
    delete r.vaoTriangles;
    r.vboTriangles.destroy();
    trianglesIdGenerator_.release(id);
}

} // namespace widgets
} // namespace vgc
