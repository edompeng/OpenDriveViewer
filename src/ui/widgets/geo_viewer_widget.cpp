#include "src/ui/widgets/geo_viewer_widget.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include "src/core/project_context.h"

namespace geoviewer::ui::widgets {

GeoViewerWidget::GeoViewerWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      renderer_(std::make_unique<renderer::SceneRenderer>()) {
  setMouseTracking(true);
  SetupSignals();
}

GeoViewerWidget::~GeoViewerWidget() = default;

void GeoViewerWidget::initializeGL() { renderer_->Initialize(); }

void GeoViewerWidget::resizeGL(int w, int h) { renderer_->Resize(w, h); }

void GeoViewerWidget::paintGL() { renderer_->Render(); }

void GeoViewerWidget::mousePressEvent(QMouseEvent*) {
  // Handle camera rotation start or selection
  update();
}

void GeoViewerWidget::mouseMoveEvent(QMouseEvent*) {
  // Handle camera update or hover info
  update();
}

void GeoViewerWidget::wheelEvent(QWheelEvent*) {
  // Handle zoom
  update();
}

void GeoViewerWidget::SetupSignals() {
  auto& context = core::ProjectContext::Instance();

  context.OnLayerVisibilityChanged([this](LayerType type, bool visible) {
    renderer_->SetLayerVisible(type, visible);
    update();
  });

  context.OnMapChanged([this](const std::string&) {
    // Possibly trigger logic to reload map data
    // Then call renderer_->UploadVertices/Indices
    update();
  });
}

}  // namespace geoviewer::ui::widgets
