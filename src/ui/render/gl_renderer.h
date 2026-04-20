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
#include "src/logic/highlight_manager.h"
#include "src/logic/routing_buffer_manager.h"
#include "src/geo_viewer_export.h"

namespace geoviewer::render {

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

  /// Get the current index count for a layer.
  size_t GetLayerIndexCount(LayerType type) const;

  /// Show/hide a layer.
  void SetLayerVisible(LayerType type, bool visible);

  /// Query layer visibility.
  bool IsLayerVisible(LayerType type) const;

  /// Set layer color and alpha.
  void SetLayerStyle(LayerType type, const QVector3D& color, float alpha);

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

  /// Returns true if user-point VAO is allocated.
  bool HasUserPointsVao() const { return user_points_vao_ != 0; }

  // ============ Measurement ============

  /// Upload measurement point vertex data.
  void UploadMeasurePointsData(const std::vector<QVector3D>& points);

  /// Returns true if measurement VAO is allocated.
  bool HasMeasureVao() const { return measure_vao_ != 0; }

  // ============ Highlighting ============

  /// Access the highlight manager for direct highlight operations.
  HighlightManager* GetHighlightManager() { return highlight_mgr_.get(); }
  const HighlightManager* GetHighlightManager() const {
    return highlight_mgr_.get();
  }

  /// Upload primary and neighbor highlight indices.
  void UploadHighlightIndices(const std::vector<uint32_t>& primary,
                              const std::vector<uint32_t>& neighbor);

  // ============ Routing ============

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
                   const std::vector<std::pair<QVector3D, bool>>& user_points,
                   size_t measure_point_count, const QVector3D& routing_color,
                   float routing_alpha);

  /// Draw all layers that use triangle draw mode.
  void DrawTriangles(GLint color_loc, GLint alpha_loc, GLint dashed_loc,
                     const QMatrix4x4& view_proj);

  /// Draw all layers that use line draw mode.
  void DrawLines(GLint color_loc, GLint alpha_loc, GLint dashed_loc);

  /// Draw user annotation points (rendered as GL_POINTS).
  void DrawPoints(
      GLint color_loc, GLint alpha_loc, GLint dashed_loc,
      const std::vector<std::pair<QVector3D, bool>>& user_points);

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
  void DrawHighlight(GLint color_loc, GLint alpha_loc, GLint dashed_loc);
  void DrawRouting(GLint color_loc, GLint alpha_loc, GLint dashed_loc,
                   const QVector3D& routing_color, float routing_alpha);
  void DrawMeasurement(GLint color_loc, GLint alpha_loc, GLint dashed_loc,
                       size_t point_count);

  // ---- Main scene buffers ----
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint shader_program_ = 0;

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
