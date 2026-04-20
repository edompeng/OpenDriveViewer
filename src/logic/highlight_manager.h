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

/// @brief 封装单层 EBO 高亮状态（SRP - 只存储一个 EBO 的状态）
struct GEOVIEWER_EXPORT HighlightBuffer {
  GLuint ebo = 0;
  size_t count = 0;
};

/// @brief 管理主高亮与邻居高亮 EBO 的生命周期及 GPU 上传 (SRP)
///
/// 设计模式: Facade（屏蔽 OpenGL EBO 细节）
/// 六大原则: SRP（高亮管理与渲染管线解耦）、DIP（通过注入函数访问 GL 上下文）
class GEOVIEWER_EXPORT HighlightManager {
 public:
  /// @param functions  OpenGL 函数接口（由 QOpenGLWidget 提供）
  explicit HighlightManager(QOpenGLExtraFunctions* functions);

  /// 一次性初始化两个 EBO
  void Initialize();

  /// 上传主高亮索引数据到 GPU
  void UploadHighlight(const std::vector<uint32_t>& indices);

  /// 上传邻居高亮索引数据到 GPU
  void UploadNeighborHighlight(const std::vector<uint32_t>& indices);

  /// 清除所有高亮数据（不释放 GPU 缓冲区）
  void Clear();

  /// 是否有激活的主高亮
  bool HasHighlight() const { return primary_.count > 0; }

  /// 是否有激活的邻居高亮
  bool HasNeighborHighlight() const { return neighbor_.count > 0; }

  const HighlightBuffer& Primary() const { return primary_; }
  const HighlightBuffer& Neighbor() const { return neighbor_; }

  // 当前高亮边界（用于相机居中）
  bool bounds_valid = false;
  QVector3D min_bound;
  QVector3D max_bound;

  // 当前高亮的区间（用于快速判断是否需要重建）
  size_t cur_start = SIZE_MAX;
  size_t cur_end = 0;
  LayerType cur_layer = LayerType::kCount;

 private:
  void Upload(HighlightBuffer& buf, const std::vector<uint32_t>& indices,
              GLenum usage);

  QOpenGLExtraFunctions* gl_;
  HighlightBuffer primary_;
  HighlightBuffer neighbor_;
};
