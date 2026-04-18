#pragma once

#include <QOpenGLWidget>
#include <memory>

#include "src/renderer/scene_renderer.h"
#include "src/core/project_context.h"

namespace geoviewer::ui::widgets {

class GeoViewerWidget : public QOpenGLWidget {
  Q_OBJECT

 public:
  explicit GeoViewerWidget(QWidget* parent = nullptr);
  ~GeoViewerWidget() override;

 protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  void SetupSignals();

  std::unique_ptr<renderer::SceneRenderer> renderer_;
};

}  // namespace geoviewer::ui::widgets
