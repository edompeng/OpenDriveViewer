#pragma once

#ifdef __APPLE__
#  define GL_SILENCE_DEPRECATION
#endif

#include <QMatrix4x4>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QSize>
#include <QVector3D>
#include <map>
#include <memory>
#include <vector>

#include "src/utility/scene_enums.h"

namespace geoviewer::renderer {

struct RenderLayer {
  unsigned int ebo = 0;
  size_t index_count = 0;
  bool visible = true;
  unsigned int draw_mode = 0x0004;  // GL_TRIANGLES
  QVector3D color{0.75f, 0.75f, 0.75f};
  float alpha = 1.0f;
  float polygon_offset_factor = 0.0f;
  float polygon_offset_units = 0.0f;
};

class SceneRenderer : protected QOpenGLExtraFunctions {
 public:
  SceneRenderer();
  ~SceneRenderer();

  void Initialize();
  void Resize(int w, int h);
  void UpdateViewMatrix(const QMatrix4x4& view);
  void UpdateProjectionMatrix(const QMatrix4x4& proj);

  void Render();

  // Buffer Updates
  void UploadVertices(const std::vector<float>& vertices);
  void UploadIndices(LayerType type, const std::vector<uint32_t>& indices);

  // Layer Management
  void SetLayerVisible(LayerType type, bool visible);
  void SetLayerStyle(LayerType type, const QVector3D& color, float alpha);
  void SetLayerDrawMode(LayerType type, unsigned int mode);
  void SetLayerPolygonOffset(LayerType type, float factor, float units);

 private:
  void InitShaders();
  void InitBuffers();

  unsigned int vao_ = 0;
  unsigned int vbo_ = 0;
  std::unique_ptr<QOpenGLShaderProgram> shader_program_;

  QMatrix4x4 view_matrix_;
  QMatrix4x4 projection_matrix_;
  QSize viewport_size_;

  std::map<LayerType, RenderLayer> layers_;
};

}  // namespace geoviewer::renderer
