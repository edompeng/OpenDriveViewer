#include "src/ui/widgets/geo_viewer.h"
#include "src/core/map_loader.h"

#include <QDebug>
#include <vector>

#include "src/core/thread_pool.h"
#include "src/logic/map_diff_analyzer.h"

void GeoViewerWidget::CompareWithMap(const QString& path) {
  if (!map_) return;

  const std::string std_path = path.toStdString();
  const auto base_map = map_;

  geoviewer::utility::ThreadPool::Instance().Enqueue(
      [this, base_map, std_path]() {
        try {
          OpenDriveMapSceneLoader loader;
          auto target_data = loader.Load(std_path);
          if (!target_data.IsValid()) {
            qWarning() << "Failed to parse OpenDRIVE map for diffing.";
            return;
          }

          auto diff = geoviewer::logic::MapDiffAnalyzer::Analyze(
              base_map, target_data.map);

          QMetaObject::invokeMethod(this, [this, diff = std::move(diff)]() {
            ApplyMapDiff(diff);
          });
        } catch (const std::exception& e) {
          qWarning() << "Error in CompareWithMap thread:" << e.what();
        }
      });
}

void GeoViewerWidget::ApplyMapDiff(const geoviewer::logic::DiffResult& diff) {
  if (!gl_renderer_ || !network_mesh_) return;

  std::vector<uint32_t> removed_indices;
  std::vector<uint32_t> modified_indices;

  const size_t lane_v_offset =
      gl_renderer_->GetLayerVertexOffset(LayerType::kLanes);

  // Collect indices for removed lanes
  for (const auto& key : diff.removed_lanes) {
    auto it = lane_element_index_by_key_.find(key);
    if (it != lane_element_index_by_key_.end()) {
      const auto& el = lane_element_items_[it->second];
      for (const auto& range : el.ranges) {
        const size_t base = static_cast<size_t>(range.start) * 3;
        for (uint32_t k = 0; k < range.count * 3; ++k) {
          removed_indices.push_back(
              network_mesh_->lanes_mesh.indices[base + k] +
              static_cast<uint32_t>(lane_v_offset));
        }
      }
    }
  }

  // Collect indices for modified lanes
  for (const auto& key : diff.modified_lanes) {
    auto it = lane_element_index_by_key_.find(key);
    if (it != lane_element_index_by_key_.end()) {
      const auto& el = lane_element_items_[it->second];
      for (const auto& range : el.ranges) {
        const size_t base = static_cast<size_t>(range.start) * 3;
        for (uint32_t k = 0; k < range.count * 3; ++k) {
          modified_indices.push_back(
              network_mesh_->lanes_mesh.indices[base + k] +
              static_cast<uint32_t>(lane_v_offset));
        }
      }
    }
  }

  // Upload to the renderer
  makeCurrent();
  gl_renderer_->GenLayerEbo(LayerType::kDiffRemoved);
  gl_renderer_->UploadLayerIndices(LayerType::kDiffRemoved, removed_indices);
  gl_renderer_->SetLayerVisible(LayerType::kDiffRemoved, true);

  gl_renderer_->GenLayerEbo(LayerType::kDiffModified);
  gl_renderer_->UploadLayerIndices(LayerType::kDiffModified, modified_indices);
  gl_renderer_->SetLayerVisible(LayerType::kDiffModified, true);
  doneCurrent();

  update();
  emit MapDiffApplied();
}

void GeoViewerWidget::ClearMapDiff() {
  if (!gl_renderer_) return;
  makeCurrent();
  gl_renderer_->UploadLayerIndices(LayerType::kDiffRemoved, {});
  gl_renderer_->UploadLayerIndices(LayerType::kDiffModified, {});
  doneCurrent();
  update();
}
