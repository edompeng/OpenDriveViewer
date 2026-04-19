#pragma once
#ifdef __APPLE__
#  define GL_SILENCE_DEPRECATION
#endif

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFuture>
#include <QHash>
#include <QMatrix4x4>
#include <QMenu>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QString>
#include <QVector3D>
#include <QWheelEvent>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "src/core/junction_grouping.h"
#include "src/core/scene_enums.h"
#include "src/core/scene_geometry_types.h"
#include "src/logic/camera_controller.h"
#include "src/logic/measure_tool_controller.h"
#include "src/ui/render/gl_renderer.h"
#include "third_party/libOpenDRIVE/include/OpenDriveMap.h"
#include "third_party/libOpenDRIVE/include/RoadNetworkMesh.h"

using LanesMesh = odr::LanesMesh;
class QPainter;

class RoutingWidget;

class GeoViewerWidget : public QOpenGLWidget {
  Q_OBJECT

 public:
  explicit GeoViewerWidget(QWidget* parent = nullptr);
  ~GeoViewerWidget() override;

  // ---------- Map Data ----------
  void SetMap(std::shared_ptr<odr::OpenDriveMap> map);
  void SetMapAndMesh(std::shared_ptr<odr::OpenDriveMap> map,
                     odr::RoadNetworkMesh mesh,
                     const JunctionClusterResult* junction_grouping = nullptr);

  std::shared_ptr<odr::OpenDriveMap> GetMap() const { return map_; }
  const JunctionClusterResult& GetJunctionClusterResult() const {
    return junction_cluster_result_;
  }
  QMatrix4x4 GetViewMatrix() const;

  // ---------- Layer Visibility ----------
  void SetLayerVisible(LayerType type, bool visible);
  bool IsLayerVisible(LayerType type) const;

  // ---------- Element Visibility ----------
  void SetElementVisible(const QString& id, bool visible);
  bool IsElementVisible(const QString& id) const;
  const std::unordered_set<std::string>& HiddenElements() const {
    return hidden_elements_;
  }

  // ---------- Measurement Feature ----------
  void SetMeasureMode(bool active);
  bool IsMeasureMode() const;
  void ClearMeasure();

  // ---------- Coordinates & Traffic Direction ----------
  void SetRightHandTraffic(bool rht);
  bool IsRightHandTraffic() const { return right_hand_traffic_; }
  void SetCoordinateMode(CoordinateMode mode) { coord_mode_ = mode; }
  CoordinateMode GetCoordinateMode() const { return coord_mode_; }
  void SetGeoreferenceAvailable(bool available) {
    georeference_valid_ = available;
  }
  bool IsGeoreferenceAvailable() const { return georeference_valid_; }

  // ---------- Batch Updates ----------
  void BeginBatchUpdate() { batch_update_count_++; }
  void EndBatchUpdate() {
    if (--batch_update_count_ == 0 && needs_index_update_) {
      UpdateMeshIndices();
    }
  }
  void UpdateMeshIndices();

  // ---------- User Annotation Points ----------
  void AddUserPoint(double lon, double lat,
                    std::optional<double> alt = std::nullopt);
  // Add point using local coordinates (x, y, z)
  void AddUserPointLocal(double x, double y,
                         std::optional<double> z = std::nullopt);
  void BeginUserPointsBatch();
  void EndUserPointsBatch();
  void RemoveUserPoint(int index);
  void SetUserPointVisible(int index, bool visible);
  void SetUserPointColor(int index, const QVector3D& color);
  void ClearUserPoints();
  int UserPointCount() const;
  struct UserPointSnapshot {
    UserPointSnapshot()
        : lon(0), lat(0), alt(0), x(0), y(0), z(0), visible(false) {}
    UserPointSnapshot(double lo, double la, double al, double px, double py,
                      double pz, bool vis, const QVector3D& col)
        : lon(lo),
          lat(la),
          alt(al),
          x(px),
          y(py),
          z(pz),
          visible(vis),
          color(col) {}

    double lon, lat, alt;
    double x, y, z;
    bool visible;
    QVector3D color;
  };

  UserPointSnapshot GetUserPointSnapshot(int index) const;

  // ---- Coordinate Transformation ----
  void RendererToLocalCoord(const QVector3D& renderer_pos, double& lx,
                            double& ly, double& lz) const;
  bool LocalToWGS84(double lx, double ly, double lz, double& lon, double& lat,
                    double& alt) const;

  // ---------- Highlighting ----------
  void HighlightElement(const QString& road_id, TreeNodeType type,
                        const QString& element_id);
  void ClearHighlight();

 signals:
  void HoverInfoChanged(double x, double y, double z, double lon, double lat,
                        double alt, const QString& type_str,
                        const QString& id_str, const QString& name_str);
  void ElementSelected(const QString& road_id, TreeNodeType type,
                       const QString& element_id);
  void AddFavoriteRequested(const QString& road_id, TreeNodeType type,
                            const QString& element_id, const QString& name);
  void RoutingStartRequested(const QString& lane_pos);
  void RoutingEndRequested(const QString& lane_pos);
  void ElementVisibilityChanged(const QString& id, bool visible);
  void TotalDistanceChanged(double distance);
  void MeasureModeChanged(bool active);
  void UserPointsChanged();

 protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* ev) override;
  void mouseReleaseEvent(QMouseEvent* ev) override;
  void mouseMoveEvent(QMouseEvent* ev) override;
  void wheelEvent(QWheelEvent* ev) override;
  void contextMenuEvent(QContextMenuEvent* ev) override;
  void focusOutEvent(QFocusEvent* event) override;

 public slots:
  void SearchObject(LayerType type, const QString& id);
  void CenterOnElement(const QString& road_id, TreeNodeType type,
                       const QString& element_id);
  void HighlightRoads(const QStringList& road_ids);
  void JumpToLocation(double lon, double lat, double alt = 0.0);
  void JumpToLocalLocation(double x, double y, double z = 0.0);

  // ---------- Multi-routing Support ----------
  int AddRoutingPath(const std::vector<odr::LaneKey>& path,
                     const QString& name);
  void RemoveRoutingPath(int id);
  void SetRoutingPathVisible(int id, bool visible);
  void ClearRoutingPaths();

 private:
  // ---- OpenGL Renderer (all GL operations delegated here) ----
  std::unique_ptr<geoviewer::render::GlRenderer> gl_renderer_;

  // ---- Delegated Components (SRP split) ----
  CameraController camera_;
  std::unique_ptr<MeasureToolController> measure_ctrl_;

  void UpdateMeasureBuffers();
  void UpdateUserPointsBuffers();

  // ---- Map Data ----
  std::shared_ptr<odr::OpenDriveMap> map_;
  odr::RoadNetworkMesh network_mesh_;
  JunctionClusterResult junction_cluster_result_;
  bool mesh_updated_ = false;
  bool needs_index_update_ = false;
  int batch_update_count_ = 0;
  std::vector<SceneCachedElement> lane_element_items_;
  std::vector<SceneCachedElement> roadmark_element_items_;
  std::vector<SceneCachedElement> object_element_items_;
  std::vector<SceneCachedElement> signal_element_items_;
  std::vector<SceneCachedElement> junction_element_items_;
  std::vector<SceneOutlineElement> outline_element_items_;
  odr::Mesh3D junction_mesh_;
  std::map<std::string, QVector3D> junction_group_centers_;
  std::unordered_map<std::string, std::size_t> junction_group_index_by_id_;
  std::unordered_map<std::string, std::size_t> junction_member_index_by_id_;
  std::vector<std::size_t> junction_vertex_group_indices_;

  std::unordered_set<std::string> hidden_elements_;
  bool right_hand_traffic_ = true;
  CoordinateMode coord_mode_ = CoordinateMode::kWGS84;
  bool georeference_valid_ = false;

  std::vector<uint32_t> lane_outline_indices_;

  std::unordered_map<odr::LaneKey, std::array<size_t, 2>> lane_key_to_interval_;
  std::unordered_map<odr::LaneKey, std::size_t> lane_element_index_by_key_;
  std::unique_ptr<odr::RoutingGraph> routing_graph_;

  struct VertRange {
    size_t start;
    size_t count;
  };
  std::map<std::string, VertRange> road_ref_line_vert_ranges_;

  // ---- Scene Construction Private Methods ----
  void GenerateRefLinePoints(std::shared_ptr<odr::OpenDriveMap> map,
                             std::vector<float>& all_vertices,
                             std::map<std::string, VertRange>& ranges);
  void BuildJunctionPlanes();
  void ResetSceneData();
  static void ClearMeshAuxiliaryData(odr::Mesh3D& mesh);
  void PopulateJunctionLookupMaps();
  void PopulateLaneKeyIntervals();
  void RebuildSceneCaches();
  void BuildLaneElementCache();
  void BuildRoadmarkElementCache();
  void BuildObjectElementCache();
  void BuildSignalElementCache();
  void BuildOutlineElementCache();
  void TransformSceneMeshes();
  std::vector<float> BuildSceneVertexBufferData();
  void UploadVertexBufferData(const std::vector<float>& vertices);
  void ApplyDefaultLayerStyles();
  void FinalizeSceneUpdate();
  std::optional<std::string> FindJunctionGroupByTriangle(
      uint32_t tri_index) const;
  std::optional<std::string> FindJunctionGroupByVertex(
      size_t vertex_index) const;
  QVector3D LocalToRendererPoint(const odr::Vec3D& point) const;
  QVector3D JunctionGroupCenter(const JunctionClusterGroup& group) const;
  void RenderJunctionOverlay(QPainter& painter, const QMatrix4x4& view_proj);
  bool IsJunctionVisible(const QString& group_id,
                         const QString& junction_id = QString()) const;
  const odr::Mesh3D* MeshForLayer(LayerType type) const;
  bool IsTrianglePickVisible(LayerType type, uint32_t triangle_index,
                             size_t vertex_index) const;
  QString selected_junction_group_id_;
  QString selected_junction_id_;

  bool IsElementActuallyVisible(const std::string& road_id,
                                const std::string& group,
                                const std::string& element_id) const;

  // ---- Spatial Grid Acceleration ----
  std::vector<SceneGridBox> grid_boxes_;
  int grid_resolution_ = 32;
  void BuildSpatialGrid();
  void StartSpatialGridBuild();
  std::vector<SceneGridBox> BuildSpatialGridData(
      std::shared_ptr<odr::OpenDriveMap> map,
      const odr::RoadNetworkMesh& network_mesh,
      const odr::Mesh3D& junction_mesh, int grid_resolution) const;
  std::atomic<uint64_t> spatial_grid_generation_{0};
  bool spatial_grid_ready_ = false;

  // ---- Ray Detection ----
  struct PickResult {
    LayerType layer;
    size_t vertex_index;
  };
  std::optional<PickResult> GetPickedVertexIndex(int x, int y);
  void UpdateHoverInfo(int x, int y);
  float closest_t_ = 0.0f;

  bool GetWorldPosAt(int x, int y, QVector3D& world_pos,
                     std::optional<PickResult>& picked_idx);
  std::vector<uint32_t> CollectIndicesForCachedElements(
      LayerType type, const std::vector<SceneCachedElement>& elements,
      const std::vector<uint32_t>& source_indices,
      const std::function<bool(const SceneCachedElement&)>& predicate) const;
  void SetHighlightIndices(const std::vector<uint32_t>& indices, LayerType type,
                           bool with_neighbors = false,
                           size_t reference_vertex = 0);
  void UpdateHighlight(size_t vert_idx, LayerType type);

  // ---- Rendering Helpers ----
  void UploadMesh3D(const odr::Mesh3D& mesh, LayerType type);
  void ComputeLaneLines(const odr::LanesMesh& mesh);
  void ReloadMeshData();
  void CalculateMeshCenter();

  // ---- User Annotation Points ----
  struct UserPoint {
    UserPoint(const QVector3D& pos, double lo, double la, double al)
        : world_pos(pos),
          lon(lo),
          lat(la),
          alt(al),
          visible(true),
          color(1.0f, 0.3f, 0.3f) {}

    QVector3D world_pos;
    double lon, lat, alt;
    bool visible;
    QVector3D color;
  };
  std::vector<UserPoint> user_points_;
  int user_points_batch_depth_ = 0;
  bool user_points_batch_dirty_ = false;
  bool user_points_batch_buffer_dirty_ = false;
  void CommitUserPointsChange(bool buffer_dirty);
};
