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
  ~HighlightManager();

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

  // Getters/Setters for bounds and range
  bool IsBoundsValid() const { return bounds_valid_; }
  const QVector3D& MinBound() const { return min_bound_; }
  const QVector3D& MaxBound() const { return max_bound_; }
  size_t CurStart() const { return cur_start_; }
  size_t CurEnd() const { return cur_end_; }
  LayerType CurLayer() const { return cur_layer_; }

  void SetBoundsValid(bool valid) { bounds_valid_ = valid; }
  void SetMinBound(const QVector3D& val) { min_bound_ = val; }
  void SetMaxBound(const QVector3D& val) { max_bound_ = val; }
  void SetCurStart(size_t val) { cur_start_ = val; }
  void SetCurEnd(size_t val) { cur_end_ = val; }
  void SetCurLayer(LayerType val) { cur_layer_ = val; }

 private:
  void Upload(HighlightBuffer& buf, const std::vector<uint32_t>& indices,
              GLenum usage);

  QOpenGLExtraFunctions* gl_;
  HighlightBuffer primary_;
  HighlightBuffer neighbor_;
  HighlightBuffer predecessor_;

  // Current highlight bounds (used for camera centering)
  bool bounds_valid_ = false;
  QVector3D min_bound_;
  QVector3D max_bound_;

  // Current highlight range
  size_t cur_start_ = SIZE_MAX;
  size_t cur_end_ = 0;
  LayerType cur_layer_ = LayerType::kCount;
};
