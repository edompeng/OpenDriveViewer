#pragma once

#ifdef __APPLE__
#  define GL_SILENCE_DEPRECATION
#endif

#include <QOpenGLExtraFunctions>
#include <QVector3D>
#include <map>
#include <memory>
#include <vector>
#include "OpenDriveMap.h"
#include "RoutingGraph.h"

/// @brief Manages the GPU buffer lifecycle of routing results (SRP)
///
/// Design Patterns: Flyweight (reusing route VAO/VBO by ID)
/// SOLID Principles:
///   SRP - Only responsible for creation/update/destruction of routing
///   rendering data DIP - Depends on QOpenGLExtraFunctions abstraction instead
///   of concrete GL functions
class RoutingBufferManager {
 public:
  struct RouteRenderData {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    size_t index_count = 0;
    bool visible = true;
    std::vector<odr::LaneKey> path;
  };

  explicit RoutingBufferManager(QOpenGLExtraFunctions* gl);
  ~RoutingBufferManager();

  /// Adds a route and builds GPU buffers, returns the allocated ID
  int Add(const std::vector<odr::LaneKey>& path,
          const std::shared_ptr<odr::OpenDriveMap>& map,
          bool right_hand_traffic);

  /// Removes the route with the specified ID
  void Remove(int id);

  /// Sets route visibility
  void SetVisible(int id, bool visible);

  /// Clears all routes
  void Clear();

  const std::map<int, RouteRenderData>& Routes() const { return routes_; }

 private:
  void BuildBuffers(int id, const std::shared_ptr<odr::OpenDriveMap>& map,
                    bool right_hand_traffic);
  void FreeRoute(RouteRenderData& route);

  QOpenGLExtraFunctions* gl_;
  std::map<int, RouteRenderData> routes_;
  int next_id_ = 1;
};
