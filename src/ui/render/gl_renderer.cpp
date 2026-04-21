#include "src/ui/render/gl_renderer.h"

#include <QDebug>
#include <algorithm>

namespace geoviewer::render {

GlRenderer::GlRenderer() {
  for (int i = 0; i < kLayerCount; ++i) {
    layers_[i].ebo = 0;
    layers_[i].index_count = 0;
    layers_[i].visible = true;
    layers_[i].polygon_offset_factor = 0.0f;
    layers_[i].polygon_offset_units = 0.0f;
    layers_[i].alpha = 1.0f;
    layers_[i].draw_mode = GL_TRIANGLES;
  }
  layers_[static_cast<int>(LayerType::kRouting)].color =
      QVector3D(0.0f, 1.0f, 0.5f);
  layers_[static_cast<int>(LayerType::kRouting)].alpha = 0.8f;
}

GlRenderer::~GlRenderer() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  for (int i = 0; i < kLayerCount; ++i) {
    if (layers_[i].ebo) glDeleteBuffers(1, &layers_[i].ebo);
  }
  if (user_points_vbo_) glDeleteBuffers(1, &user_points_vbo_);
  if (user_points_vao_) glDeleteVertexArrays(1, &user_points_vao_);
  if (measure_vbo_) glDeleteBuffers(1, &measure_vbo_);
  if (measure_vao_) glDeleteVertexArrays(1, &measure_vao_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (shader_program_) glDeleteProgram(shader_program_);
}

// ============ Initialization ============

bool GlRenderer::Initialize() {
  initializeOpenGLFunctions();

  glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Create delegated sub-components with GL function interface
  auto* gl_funcs = static_cast<QOpenGLExtraFunctions*>(this);
  highlight_mgr_ = std::make_unique<HighlightManager>(gl_funcs);
  routing_buf_mgr_ = std::make_unique<RoutingBufferManager>(gl_funcs);

  if (!InitShaders()) {
    return false;
  }
  InitBuffers();
  return true;
}

void GlRenderer::Resize(int w, int h) {
  viewport_size_ = QSize(w, h);
  glViewport(0, 0, w, h);
}

// ============ Scene Vertex Data ============

void GlRenderer::UploadSceneVertices(const std::vector<float>& vertices) {
  if (vertices.empty() || !vbo_) return;

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
               vertices.data(), GL_STATIC_DRAW);
  glBindVertexArray(0);
}

// ============ Layer Management ============

void GlRenderer::GenLayerEbo(LayerType type) {
  int idx = static_cast<int>(type);
  if (!layers_[idx].ebo) {
    glGenBuffers(1, &layers_[idx].ebo);
  }
}

void GlRenderer::UploadLayerIndices(LayerType type,
                                    const std::vector<uint32_t>& indices) {
  int idx = static_cast<int>(type);
  layers_[idx].index_count = indices.size();
  if (indices.empty()) {
    return;
  }
  
  GenLayerEbo(type);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layers_[idx].ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
               indices.data(), GL_STATIC_DRAW);
}

void GlRenderer::SetLayerVertexOffset(LayerType type, size_t offset) {
  layers_[static_cast<int>(type)].vertex_offset = offset;
}

size_t GlRenderer::GetLayerVertexOffset(LayerType type) const {
  return layers_[static_cast<int>(type)].vertex_offset;
}

void GlRenderer::SetLayerChunks(LayerType type,
                                std::vector<SceneMeshChunk> chunks) {
  layers_[static_cast<int>(type)].chunks = std::move(chunks);
}

size_t GlRenderer::GetLayerIndexCount(LayerType type) const {
  return layers_[static_cast<int>(type)].index_count;
}

void GlRenderer::SetLayerVisible(LayerType type, bool visible) {
  if (type >= LayerType::kLanes && type < LayerType::kCount) {
    layers_[static_cast<int>(type)].visible = visible;
  }
}

bool GlRenderer::IsLayerVisible(LayerType type) const {
  if (type >= LayerType::kLanes && type < LayerType::kCount) {
    return layers_[static_cast<int>(type)].visible;
  }
  return false;
}

void GlRenderer::SetLayerStyle(LayerType type, const QVector3D& color,
                               float alpha) {
  int idx = static_cast<int>(type);
  layers_[idx].color = color;
  layers_[idx].alpha = alpha;
}

void GlRenderer::SetLayerColor(LayerType type, const QVector3D& color) {
  layers_[static_cast<int>(type)].color = color;
}

void GlRenderer::SetLayerAlpha(LayerType type, float alpha) {
  layers_[static_cast<int>(type)].alpha = alpha;
}

void GlRenderer::SetLayerDrawMode(LayerType type, GLenum mode) {
  layers_[static_cast<int>(type)].draw_mode = mode;
}

void GlRenderer::SetLayerPolygonOffset(LayerType type, float factor,
                                       float units) {
  int idx = static_cast<int>(type);
  layers_[idx].polygon_offset_factor = factor;
  layers_[idx].polygon_offset_units = units;
}

// ============ User Points ============

void GlRenderer::UploadUserPointsData(const std::vector<float>& data) {
  if (data.empty()) return;

  if (!user_points_vao_) {
    glGenVertexArrays(1, &user_points_vao_);
    glGenBuffers(1, &user_points_vbo_);
  }

  glBindVertexArray(user_points_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, user_points_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(data.size() * sizeof(float)),
               data.data(), GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                        static_cast<void*>(nullptr));
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

// ============ Measurement ============

void GlRenderer::UploadMeasurePointsData(
    const std::vector<QVector3D>& points) {
  if (points.empty()) return;

  if (!measure_vao_) {
    glGenVertexArrays(1, &measure_vao_);
    glGenBuffers(1, &measure_vbo_);
  }

  glBindVertexArray(measure_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, measure_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(points.size() * sizeof(QVector3D)),
               points.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QVector3D), nullptr);
  glBindVertexArray(0);
}

// ============ Highlighting ============

void GlRenderer::UploadHighlightIndices(
    const std::vector<uint32_t>& primary,
    const std::vector<uint32_t>& neighbor) {
  if (!highlight_mgr_) return;
  highlight_mgr_->UploadHighlight(primary);
  highlight_mgr_->UploadNeighborHighlight(neighbor);
}

// ============ Core Rendering ============

void GlRenderer::RenderScene(
    const QMatrix4x4& view, float distance, float mesh_radius,
    const std::vector<std::pair<QVector3D, bool>>& user_points,
    size_t measure_point_count, const QVector3D& routing_color,
    float routing_alpha) {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram(shader_program_);

  // Model matrix (identity)
  QMatrix4x4 model;
  model.setToIdentity();
  glUniformMatrix4fv(glGetUniformLocation(shader_program_, "model"), 1,
                     GL_FALSE, model.data());

  // View matrix
  glUniformMatrix4fv(glGetUniformLocation(shader_program_, "view"), 1, GL_FALSE,
                     view.data());

  // Projection matrix
  float aspect =
      static_cast<float>(viewport_size_.width()) / viewport_size_.height();

  float near_plane = qMax(0.1f, distance * 0.01f);
  float far_plane = distance + mesh_radius * 2.0f + 1000.0f;

  const float kMaxRatio = 1000000.0f;
  if (far_plane / near_plane > kMaxRatio) {
    near_plane = far_plane / kMaxRatio;
  }
  if (near_plane > distance * 0.5f) {
    near_plane = distance * 0.5f;
    far_plane = near_plane * kMaxRatio;
  }

  proj_.setToIdentity();
  proj_.perspective(45.0f, aspect, near_plane, far_plane);
  glUniformMatrix4fv(glGetUniformLocation(shader_program_, "projection"), 1,
                     GL_FALSE, proj_.data());

  GLint color_loc = glGetUniformLocation(shader_program_, "objectColor");
  GLint alpha_loc = glGetUniformLocation(shader_program_, "alpha");
  GLint dashed_loc = glGetUniformLocation(shader_program_, "is_dashed");

  glBindVertexArray(vao_);

  // Compute view-projection matrix for frustum culling
  QMatrix4x4 view_proj = proj_ * view;

  // Draw all layers (triangles and lines)
  DrawTriangles(color_loc, alpha_loc, dashed_loc, view_proj);
  DrawLines(color_loc, alpha_loc, dashed_loc);

  // Draw highlighting
  DrawHighlight(color_loc, alpha_loc, dashed_loc);

  // Draw user annotation points
  DrawPoints(color_loc, alpha_loc, dashed_loc, user_points);

  // Draw routing results
  DrawRouting(color_loc, alpha_loc, dashed_loc, routing_color, routing_alpha);

  // Draw measurement
  DrawMeasurement(color_loc, alpha_loc, dashed_loc, measure_point_count);

  glBindVertexArray(0);
}

void GlRenderer::DrawTriangles(GLint color_loc, GLint alpha_loc,
                                GLint dashed_loc,
                                const QMatrix4x4& view_proj) {
  for (int i = 0; i < kLayerCount; ++i) {
    if (!layers_[i].visible || layers_[i].index_count == 0 || !layers_[i].ebo) {
      continue;
    }
    if (layers_[i].draw_mode != GL_TRIANGLES) continue;

    if (layers_[i].polygon_offset_factor != 0.0f ||
        layers_[i].polygon_offset_units != 0.0f) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glEnable(GL_POLYGON_OFFSET_LINE);
      glPolygonOffset(layers_[i].polygon_offset_factor,
                      layers_[i].polygon_offset_units);
    }

    glUniform3f(color_loc, layers_[i].color.x(), layers_[i].color.y(),
                layers_[i].color.z());
    glUniform1f(alpha_loc, layers_[i].alpha);
    glUniform1i(dashed_loc,
                (i == static_cast<int>(LayerType::kLaneLinesDashed)) ? 1 : 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layers_[i].ebo);

    if (layers_[i].chunks.empty()) {
      glDrawElements(layers_[i].draw_mode,
                     static_cast<GLsizei>(layers_[i].index_count),
                     GL_UNSIGNED_INT, nullptr);
    } else {
      auto IsAabbVisible = [&](const QVector3D& min_b, const QVector3D& max_b) {
        QVector3D corners[8] = {
            {min_b.x(), min_b.y(), min_b.z()},
            {max_b.x(), min_b.y(), min_b.z()},
            {min_b.x(), max_b.y(), min_b.z()},
            {max_b.x(), max_b.y(), min_b.z()},
            {min_b.x(), min_b.y(), max_b.z()},
            {max_b.x(), min_b.y(), max_b.z()},
            {min_b.x(), max_b.y(), max_b.z()},
            {max_b.x(), max_b.y(), max_b.z()}};

        bool all_out[6] = {true, true, true, true, true, true};
        for (int c = 0; c < 8; ++c) {
          QVector4D pt(corners[c], 1.0f);
          pt = view_proj * pt;

          if (pt.w() > 0) {
            if (pt.x() >= -pt.w()) all_out[0] = false;
            if (pt.x() <= pt.w()) all_out[1] = false;
            if (pt.y() >= -pt.w()) all_out[2] = false;
            if (pt.y() <= pt.w()) all_out[3] = false;
            if (pt.z() >= -pt.w()) all_out[4] = false;
            if (pt.z() <= pt.w()) all_out[5] = false;
          } else {
            return true;
          }
        }
        for (int p = 0; p < 6; ++p) {
          if (all_out[p]) return false;
        }
        return true;
      };

      for (const auto& chunk : layers_[i].chunks) {
        if (IsAabbVisible(chunk.min_bound, chunk.max_bound)) {
          glDrawElements(
              layers_[i].draw_mode, static_cast<GLsizei>(chunk.index_count),
              GL_UNSIGNED_INT,
              reinterpret_cast<void*>(
                  static_cast<intptr_t>(chunk.index_offset * sizeof(uint32_t))));
        }
      }
    }

    if (layers_[i].polygon_offset_factor != 0.0f ||
        layers_[i].polygon_offset_units != 0.0f) {
      glDisable(GL_POLYGON_OFFSET_FILL);
      glDisable(GL_POLYGON_OFFSET_LINE);
    }
  }
}

void GlRenderer::DrawLines(GLint color_loc, GLint alpha_loc,
                            GLint dashed_loc) {
  for (int i = 0; i < kLayerCount; ++i) {
    if (!layers_[i].visible || layers_[i].index_count == 0 || !layers_[i].ebo) {
      continue;
    }
    if (layers_[i].draw_mode != GL_LINES) continue;

    glLineWidth(2.0f);

    if (layers_[i].polygon_offset_factor != 0.0f ||
        layers_[i].polygon_offset_units != 0.0f) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glEnable(GL_POLYGON_OFFSET_LINE);
      glPolygonOffset(layers_[i].polygon_offset_factor,
                      layers_[i].polygon_offset_units);
    }

    glUniform3f(color_loc, layers_[i].color.x(), layers_[i].color.y(),
                layers_[i].color.z());
    glUniform1f(alpha_loc, layers_[i].alpha);
    glUniform1i(dashed_loc,
                (i == static_cast<int>(LayerType::kLaneLinesDashed)) ? 1 : 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layers_[i].ebo);
    glDrawElements(layers_[i].draw_mode,
                   static_cast<GLsizei>(layers_[i].index_count),
                   GL_UNSIGNED_INT, nullptr);

    if (layers_[i].polygon_offset_factor != 0.0f ||
        layers_[i].polygon_offset_units != 0.0f) {
      glDisable(GL_POLYGON_OFFSET_FILL);
      glDisable(GL_POLYGON_OFFSET_LINE);
    }
  }
}

void GlRenderer::DrawPoints(
    GLint color_loc, GLint alpha_loc, GLint dashed_loc,
    const std::vector<std::pair<QVector3D, bool>>& user_points) {
  if (user_points.empty() || !user_points_vao_) return;

  glDisable(GL_DEPTH_TEST);
  glPointSize(10.0f);
  glUniform1f(alpha_loc, 1.0f);
  glUniform1i(dashed_loc, 0);
  glBindVertexArray(user_points_vao_);
  for (int i = 0; i < static_cast<int>(user_points.size()); ++i) {
    if (!user_points[i].second) continue;  // Skip invisible points
    const auto& c = user_points[i].first;
    glUniform3f(color_loc, c.x(), c.y(), c.z());
    glDrawArrays(GL_POINTS, i, 1);
  }
  glEnable(GL_DEPTH_TEST);
}

void GlRenderer::DrawHighlight(GLint color_loc, GLint alpha_loc,
                                GLint dashed_loc) {
  // Primary highlight (green)
  if (highlight_mgr_ && highlight_mgr_->HasHighlight()) {
    glBindVertexArray(vao_);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    glUniform3f(color_loc, 0.2f, 0.85f, 0.4f);
    glUniform1f(alpha_loc, 1.0f);
    glUniform1i(dashed_loc, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, highlight_mgr_->Primary().ebo);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(highlight_mgr_->Primary().count),
                   GL_UNSIGNED_INT, nullptr);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Neighbor highlight (orange)
  if (highlight_mgr_ && highlight_mgr_->HasNeighborHighlight()) {
    glBindVertexArray(vao_);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.8f, -1.8f);
    glUniform3f(color_loc, 1.0f, 0.5f, 0.0f);
    glUniform1f(alpha_loc, 0.8f);
    glUniform1i(dashed_loc, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, highlight_mgr_->Neighbor().ebo);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(highlight_mgr_->Neighbor().count),
                   GL_UNSIGNED_INT, nullptr);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
}

void GlRenderer::DrawRouting(GLint color_loc, GLint alpha_loc,
                              GLint dashed_loc,
                              const QVector3D& routing_color,
                              float routing_alpha) {
  if (!routing_buf_mgr_) return;

  for (const auto& [id, route] : routing_buf_mgr_->Routes()) {
    if (route.visible && route.index_count > 0 && route.vao) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(-3.0f, -3.0f);
      glUniform3f(color_loc, routing_color.x(), routing_color.y(),
                  routing_color.z());
      glUniform1f(alpha_loc, routing_alpha);
      glUniform1i(dashed_loc, 0);
      glBindVertexArray(route.vao);
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(route.index_count),
                     GL_UNSIGNED_INT, nullptr);
      glDisable(GL_POLYGON_OFFSET_FILL);
    }
  }
}

void GlRenderer::DrawMeasurement(GLint color_loc, GLint alpha_loc,
                                  GLint dashed_loc, size_t point_count) {
  if (point_count == 0 || !measure_vao_) return;

  glDisable(GL_DEPTH_TEST);
  glLineWidth(3.0f);
  glPointSize(8.0f);
  glUniform3f(color_loc, 1.0f, 1.0f, 0.2f);  // Yellow
  glUniform1f(alpha_loc, 1.0f);
  glUniform1i(dashed_loc, 0);
  glBindVertexArray(measure_vao_);
  if (point_count >= 2) {
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(point_count));
  }
  glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(point_count));
  glEnable(GL_DEPTH_TEST);
}

// ============ Shader Setup ============

bool GlRenderer::InitShaders() {
  const char* vertex_shader_source = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        out vec3 vWorldPos;
        void main() {
            vec4 world_pos = model * vec4(aPos, 1.0);
            vWorldPos = world_pos.xyz;
            gl_Position = projection * view * world_pos;
        }
    )";
  const char* fragment_shader_source = R"(
    #version 330 core
    out vec4 FragColor;
    in vec3 vWorldPos;
    uniform vec3 objectColor;
    uniform float alpha;
    uniform bool is_dashed;
    void main() {
        if (is_dashed) {
            float d = length(vWorldPos.xy) * 2.0;
            if (fract(d) > 0.5) discard;
        }
        FragColor = vec4(objectColor, alpha);
    }
    )";

  GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
  glCompileShader(vertex_shader);

  GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr);
  glCompileShader(fragment_shader);

  if (!CheckShaderErrors(vertex_shader, "VERTEX") ||
      !CheckShaderErrors(fragment_shader, "FRAGMENT")) {
    return false;
  }

  shader_program_ = glCreateProgram();
  glAttachShader(shader_program_, vertex_shader);
  glAttachShader(shader_program_, fragment_shader);
  glLinkProgram(shader_program_);

  if (!CheckProgramErrors(shader_program_)) {
    return false;
  }
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  return true;
}

void GlRenderer::InitBuffers() {
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);
  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);

  // Initialize highlight buffers
  if (highlight_mgr_) {
    highlight_mgr_->Initialize();
  }

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                        static_cast<void*>(nullptr));
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
}

bool GlRenderer::CheckShaderErrors(GLuint shader, const char* type) {
  GLint success;
  GLchar info_log[1024];
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(shader, 1024, nullptr, info_log);
    qCritical() << "Shader error" << type << ":" << info_log;
    return false;
  }
  return true;
}

bool GlRenderer::CheckProgramErrors(GLuint program) {
  GLint success;
  GLchar info_log[1024];
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(program, 1024, nullptr, info_log);
    qCritical() << "Program link error:" << info_log;
    return false;
  }
  return true;
}

}  // namespace geoviewer::render
