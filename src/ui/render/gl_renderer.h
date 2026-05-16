#pragma once

#ifdef __APPLE__
#  define GL_SILENCE_DEPRECATION
#endif

#include <QMatrix4x4>
#include <QOpenGLExtraFunctions>
#include <QSize>
#include <QVector3D>
#include <cstdint>
#include <memory>
#include <vector>

#include "src/core/scene_enums.h"
#include "src/core/scene_geometry_types.h"
#include "src/geo_viewer_export.h"
#include "src/logic/highlight_manager.h"
#include "src/logic/routing_buffer_manager.h"

namespace geoviewer::render {

/// @brief Cached uniform locations for optimized rendering
struct FrustumPlane {
  QVector3D normal;
  float distance;
  bool IsPointInFront(const QVector3D& p) const {
    return QVector3D::dotProduct(normal, p) + distance >= 0;
  }
};

struct Frustum {
  FrustumPlane planes[6];
  bool IsAabbVisible(const QVector3D& min_b, const QVector3D& max_b) const {
    const float min_x = min_b.x(), min_y = min_b.y(), min_z = min_b.z();
    const float max_x = max_b.x(), max_y = max_b.y(), max_z = max_b.z();

    for (int i = 0; i < 6; ++i) {
      const float nx = planes[i].normal.x();
      const float ny = planes[i].normal.y();
      const float nz = planes[i].normal.z();

      const float px = (nx >= 0) ? max_x : min_x;
      const float py = (ny >= 0) ? max_y : min_y;
      const float pz = (nz >= 0) ? max_z : min_z;

      if (nx * px + ny * py + nz * pz + planes[i].distance < 0) return false;
    }
    return true;
  }
};

struct ShaderUniforms {
  GLint model = -1;
  GLint view = -1;
  GLint projection = -1;
  GLint object_color = -1;
  GLint alpha = -1;
  GLint is_dashed = -1;
  GLint use_vertex_color = -1;
};

/// @brief Single layer OpenGL mesh descriptor (Data Class)
struct MeshLayer {
  GLuint ebo = 0;
  size_t index_count = 0;
  bool visible = true;
  GLenum draw_mode = GL_TRIANGLES;
  QVector3D color{0.75f, 0.75f, 0.75f};
  float alpha = 1.0f;
  float polygon_offset_factor = 0.0f;
  float polygon_offset_units = 0.0f;
  size_t vertex_offset = 0;
  QVector3D layer_min{1e9f, 1e9f, 1e9f};
  QVector3D layer_max{-1e9f, -1e9f, -1e9f};
  std::vector<SceneMeshChunk> chunks;
};

/// @brief Centralized OpenGL renderer. All OpenGL calls go through this class.
///
/// Design Pattern: Facade (hides OpenGL complexity behind a clean interface)
/// SOLID: SRP (only OpenGL rendering), DIP (external code depends on this
///        abstraction rather than raw GL calls)
///
/// Usage: A single instance is created by the QOpenGLWidget. The widget must
/// call makeCurrent() before invoking any method that issues GL commands, and
/// doneCurrent() when finished.
class GEOVIEWER_EXPORT GlRenderer : protected QOpenGLExtraFunctions {
 public:
  GlRenderer();
  ~GlRenderer();

  // Non-copyable, non-movable
  GlRenderer(const GlRenderer&) = delete;
  GlRenderer& operator=(const GlRenderer&) = delete;

  // ============ Initialization ============

  /// Initialize OpenGL functions, shaders, and buffers.
  /// Must be called after the GL context is current (inside initializeGL).
  bool Initialize();

  /// Handle viewport resize. Called from resizeGL.
  void Resize(int w, int h);

  /// Clear all scene data, managers and highlights.
  void Clear();

  // ============ Scene Vertex Data ============

  /// Upload the combined scene vertex buffer (all layers share one VBO).
  void UploadSceneVertices(const std::vector<float>& vertices);

  // ============ Layer Management ============

  /// Ensure the EBO for the given layer exists.
  void GenLayerEbo(LayerType type);

  /// Upload index data for a specific layer.
  void UploadLayerIndices(LayerType type, const std::vector<uint32_t>& indices);

  /// Set the vertex offset for a layer (base index into the shared VBO).
  void SetLayerVertexOffset(LayerType type, size_t offset);

  /// Get the vertex offset for a layer.
  size_t GetLayerVertexOffset(LayerType type) const;

  /// Set frustum-culling chunks for a layer.
  void SetLayerChunks(LayerType type, std::vector<SceneMeshChunk> chunks);

  /// Show/hide a layer.
  void SetLayerVisible(LayerType type, bool visible);

  /// Query layer visibility.
  bool IsLayerVisible(LayerType type) const;

  /// Set layer color.
  void SetLayerColor(LayerType type, const QVector3D& color);

  /// Set layer alpha.
  void SetLayerAlpha(LayerType type, float alpha);

  /// Set the OpenGL draw mode for a layer (GL_TRIANGLES, GL_LINES, etc.)
  void SetLayerDrawMode(LayerType type, GLenum mode);

  /// Set polygon offset for a layer.
  void SetLayerPolygonOffset(LayerType type, float factor, float units);

  // ============ User Points ============

  /// Upload user-annotation point vertex data.
  void UploadUserPointsData(const std::vector<float>& data);

  // ============ Measurement ============

  /// Upload measurement point vertex data.
  void UploadMeasurePointsData(const std::vector<QVector3D>& points);

  // ============ Highlighting ============

  /// Access the highlight manager for direct highlight operations.
  HighlightManager* GetHighlightManager() { return highlight_mgr_.get(); }
  const HighlightManager* GetHighlightManager() const {
    return highlight_mgr_.get();
  }

  /// Access the routing buffer manager for direct routing operations.
  RoutingBufferManager* GetRoutingBufferManager() {
    return routing_buf_mgr_.get();
  }

  // ============ Core Rendering ============

  /// Render the entire scene. This is the main entry point called from
  /// paintGL. It handles clearing, setting up matrices, and drawing all
  /// layers, highlights, user points, routing paths, and measurements.
  ///
  /// @param view        View matrix from camera
  /// @param distance    Camera distance (for near/far plane calculation)
  /// @param mesh_radius Radius of loaded mesh (for far plane)
  /// @param user_points User point data for per-point color rendering
  /// @param measure_point_count Number of measurement points to draw
  /// @param routing_color Color for routing paths
  /// @param routing_alpha Alpha for routing paths
  void RenderScene(const QMatrix4x4& view, float distance, float mesh_radius,
                   size_t user_point_count, size_t measure_point_count,
                   const QVector3D& routing_color, float routing_alpha);

  /// Draw all layers that use triangle draw mode.
  void DrawTriangles();

  /// Draw all layers that use line draw mode.
  void DrawLines();

  /// Draw user annotation points (rendered as GL_POINTS).
  void DrawPoints(size_t point_count);

  // ============ Projection Utilities ============

  /// Get the current projection matrix.
  const QMatrix4x4& GetProjectionMatrix() const { return proj_; }

  /// Get the current viewport size.
  QSize GetViewportSize() const { return viewport_size_; }

 private:
  // ---- Shader setup ----
  bool InitShaders();
  void InitBuffers();
  bool CheckShaderErrors(GLuint shader, const char* type);
  bool CheckProgramErrors(GLuint program);

  // ---- Internal draw helpers ----
  void DrawHighlight();
  void DrawRouting(const QVector3D& routing_color, float routing_alpha);
  void DrawMeasurement(size_t point_count);

  // ---- Main scene buffers ----
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  void UpdateFrustum(const QMatrix4x4& view_proj);
  Frustum frustum_;

  GLuint shader_program_ = 0;
  ShaderUniforms uniforms_;

  // ---- Layers ----
  static constexpr int kLayerCount = static_cast<int>(LayerType::kCount);
  MeshLayer layers_[kLayerCount];

  // ---- User points ----
  GLuint user_points_vao_ = 0;
  GLuint user_points_vbo_ = 0;

  // ---- Measurement ----
  GLuint measure_vao_ = 0;
  GLuint measure_vbo_ = 0;

  // ---- Delegated sub-components ----
  std::unique_ptr<HighlightManager> highlight_mgr_;
  std::unique_ptr<RoutingBufferManager> routing_buf_mgr_;

  // ---- Projection state ----
  QMatrix4x4 proj_;
  QSize viewport_size_;
};

}  // namespace geoviewer::render
