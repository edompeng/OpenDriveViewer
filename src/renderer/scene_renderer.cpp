#include "src/renderer/scene_renderer.h"
#include <QDebug>

namespace geoviewer::renderer {

SceneRenderer::SceneRenderer() {
  layers_[LayerType::kLanes] = {0, 0, true, 0x0004, {0.75f, 0.75f, 0.75f}, 1.0f, 0.0f, 0.0f};
  layers_[LayerType::kRouting] = {0, 0, true, 0x0004, {0.0f, 1.0f, 0.5f}, 0.8f, 0.0f, 0.0f};
  // ... initialize other layers as needed
}

SceneRenderer::~SceneRenderer() {
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  for (auto& pair : layers_) {
    if (pair.second.ebo) glDeleteBuffers(1, &pair.second.ebo);
  }
}

void SceneRenderer::Initialize() {
  initializeOpenGLFunctions();
  InitShaders();
  InitBuffers();
}

void SceneRenderer::Resize(int w, int h) {
  viewport_size_ = QSize(w, h);
  glViewport(0, 0, w, h);
}

void SceneRenderer::UpdateViewMatrix(const QMatrix4x4& view) {
  view_matrix_ = view;
}

void SceneRenderer::UpdateProjectionMatrix(const QMatrix4x4& proj) {
  projection_matrix_ = proj;
}

void SceneRenderer::Render() {
  if (!shader_program_ || !shader_program_->isLinked()) return;

  shader_program_->bind();
  
  shader_program_->setUniformValue("model", QMatrix4x4());
  shader_program_->setUniformValue("view", view_matrix_);
  shader_program_->setUniformValue("projection", projection_matrix_);

  glBindVertexArray(vao_);

  for (auto& [type, layer] : layers_) {
    if (!layer.visible || layer.index_count == 0 || !layer.ebo) continue;

    if (layer.draw_mode == 0x0001 /* GL_LINES */) {
      glLineWidth(2.0f);
    }

    if (layer.polygon_offset_factor != 0.0f || layer.polygon_offset_units != 0.0f) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glEnable(GL_POLYGON_OFFSET_LINE);
      glPolygonOffset(layer.polygon_offset_factor, layer.polygon_offset_units);
    }

    shader_program_->setUniformValue("objectColor", layer.color);
    shader_program_->setUniformValue("alpha", layer.alpha);
    shader_program_->setUniformValue("is_dashed", (type == LayerType::kLaneLinesDashed) ? 1 : 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layer.ebo);
    glDrawElements(layer.draw_mode, static_cast<GLsizei>(layer.index_count), GL_UNSIGNED_INT, nullptr);

    if (layer.polygon_offset_factor != 0.0f || layer.polygon_offset_units != 0.0f) {
      glDisable(GL_POLYGON_OFFSET_FILL);
      glDisable(GL_POLYGON_OFFSET_LINE);
    }
  }

  glBindVertexArray(0);
  shader_program_->release();
}

void SceneRenderer::UploadVertices(const std::vector<float>& vertices) {
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
  
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  
  glBindVertexArray(0);
}

void SceneRenderer::UploadIndices(LayerType type, const std::vector<uint32_t>& indices) {
  auto& layer = layers_[type];
  if (!layer.ebo) glGenBuffers(1, &layer.ebo);
  
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layer.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
  layer.index_count = indices.size();
}

void SceneRenderer::SetLayerVisible(LayerType type, bool visible) {
  layers_[type].visible = visible;
}

void SceneRenderer::SetLayerStyle(LayerType type, const QVector3D& color, float alpha) {
  layers_[type].color = color;
  layers_[type].alpha = alpha;
}

void SceneRenderer::SetLayerDrawMode(LayerType type, unsigned int mode) {
  layers_[type].draw_mode = mode;
}

void SceneRenderer::SetLayerPolygonOffset(LayerType type, float factor, float units) {
  layers_[type].polygon_offset_factor = factor;
  layers_[type].polygon_offset_units = units;
}

void SceneRenderer::InitShaders() {
  shader_program_ = std::make_unique<QOpenGLShaderProgram>();
  
  const char* vshader = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    void main() {
        gl_Position = projection * view * model * vec4(aPos, 1.0);
    }
  )";

  const char* fshader = R"(
    #version 330 core
    out vec4 FragColor;
    uniform vec3 objectColor;
    uniform float alpha;
    uniform int is_dashed;
    void main() {
        if (is_dashed == 1) {
            // Simple dash logic based on screen space or such could be here
        }
        FragColor = vec4(objectColor, alpha);
    }
  )";

  shader_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vshader);
  shader_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fshader);
  shader_program_->link();
}

void SceneRenderer::InitBuffers() {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
}

}  // namespace geoviewer::renderer
