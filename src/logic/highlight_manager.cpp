#include "src/logic/highlight_manager.h"

HighlightManager::HighlightManager(QOpenGLExtraFunctions* functions)
    : gl_(functions) {}

void HighlightManager::Initialize() {
  gl_->glGenBuffers(1, &primary_.ebo);
  gl_->glGenBuffers(1, &neighbor_.ebo);
}

void HighlightManager::UploadHighlight(const std::vector<uint32_t>& indices) {
  Upload(primary_, indices, GL_DYNAMIC_DRAW);
}

void HighlightManager::UploadNeighborHighlight(
    const std::vector<uint32_t>& indices) {
  Upload(neighbor_, indices, GL_DYNAMIC_DRAW);
}

void HighlightManager::Clear() {
  primary_.count = 0;
  neighbor_.count = 0;
  bounds_valid = false;
  cur_start = SIZE_MAX;
  cur_end = 0;
  cur_layer = LayerType::kCount;
}

void HighlightManager::Upload(HighlightBuffer& buf,
                              const std::vector<uint32_t>& indices,
                              GLenum usage) {
  buf.count = indices.size();
  if (buf.count == 0 || !buf.ebo) return;
  gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.ebo);
  gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                    indices.data(), usage);
}
