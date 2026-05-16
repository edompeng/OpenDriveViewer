#pragma once

#ifdef __APPLE__
#  define GL_SILENCE_DEPRECATION
#endif

#include <QOpenGLExtraFunctions>
#include <QVector3D>
#include <cstdint>
#include <vector>

#include "src/core/scene_enums.h"
#include "src/geo_viewer_export.h"

/// @brief Encapsulates highlight state for a single EBO layer (SRP - stores
/// only one EBO state)
struct GEOVIEWER_EXPORT HighlightBuffer {
  GLuint ebo = 0;
  size_t count = 0;
};

/// @brief Manages the lifecycle and GPU upload of primary and neighbor
/// highlight EBOs (SRP)
///
/// Design Pattern: Facade (hides OpenGL EBO details)
/// Principles: SRP (decouples highlight management from the rendering
/// pipeline), DIP (accesses GL context via dependency injection)
class GEOVIEWER_EXPORT HighlightManager {
 public:
  /// @param functions  OpenGL function interface (provided by QOpenGLWidget)
  explicit HighlightManager(QOpenGLExtraFunctions* functions);

  /// Initialize all highlight EBOs
  void Initialize();

  /// Upload primary highlight index data to the GPU
  void UploadHighlight(const std::vector<uint32_t>& indices);

  /// Upload neighbor highlight index data to the GPU
  void UploadNeighborHighlight(const std::vector<uint32_t>& indices);

  /// Upload predecessor highlight index data to the GPU
  void UploadPredecessorHighlight(const std::vector<uint32_t>& indices);

  /// Clear all highlight data (does not release GPU buffers)
  void Clear();

  /// Whether there is an active primary highlight
  bool HasHighlight() const { return primary_.count > 0; }

  /// Whether there is an active neighbor highlight
  bool HasNeighborHighlight() const { return neighbor_.count > 0; }

  /// Whether there is an active predecessor highlight
  bool HasPredecessorHighlight() const { return predecessor_.count > 0; }

  const HighlightBuffer& Primary() const { return primary_; }
  const HighlightBuffer& Neighbor() const { return neighbor_; }
  const HighlightBuffer& Predecessor() const { return predecessor_; }

  // Current highlight bounds (used for camera centering)
  bool bounds_valid = false;
  QVector3D min_bound;
  QVector3D max_bound;

  // Current highlight range (used to quickly determine if reconstruction is
  // needed)
  size_t cur_start = SIZE_MAX;
  size_t cur_end = 0;
  LayerType cur_layer = LayerType::kCount;

 private:
  void Upload(HighlightBuffer& buf, const std::vector<uint32_t>& indices,
              GLenum usage);

  QOpenGLExtraFunctions* gl_;
  HighlightBuffer primary_;
  HighlightBuffer neighbor_;
  HighlightBuffer predecessor_;
};
