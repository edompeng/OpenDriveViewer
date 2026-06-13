#include "src/logic/highlight_manager.h"

HighlightManager::HighlightManager(QOpenGLExtraFunctions* functions)
    : gl_(functions) {}

HighlightManager::~HighlightManager() {
  if (gl_) {
    if (primary_.ebo) gl_->glDeleteBuffers(1, &primary_.ebo);
    if (neighbor_.ebo) gl_->glDeleteBuffers(1, &neighbor_.ebo);
    if (predecessor_.ebo) gl_->glDeleteBuffers(1, &predecessor_.ebo);
  }
}

void HighlightManager::Initialize() {
  if (!primary_.ebo) gl_->glGenBuffers(1, &primary_.ebo);
  if (!neighbor_.ebo) gl_->glGenBuffers(1, &neighbor_.ebo);
  if (!predecessor_.ebo) gl_->glGenBuffers(1, &predecessor_.ebo);
}

void HighlightManager::UploadHighlight(const std::vector<uint32_t>& indices) {
  Upload(primary_, indices, GL_DYNAMIC_DRAW);
}

void HighlightManager::UploadNeighborHighlight(
    const std::vector<uint32_t>& indices) {
  Upload(neighbor_, indices, GL_DYNAMIC_DRAW);
}

void HighlightManager::UploadPredecessorHighlight(
    const std::vector<uint32_t>& indices) {
  Upload(predecessor_, indices, GL_DYNAMIC_DRAW);
}

void HighlightManager::Clear() {
  primary_.count = 0;
  neighbor_.count = 0;
  predecessor_.count = 0;
  bounds_valid_ = false;
  cur_start_ = SIZE_MAX;
  cur_end_ = 0;
  cur_layer_ = LayerType::kCount;
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
