#pragma once

#include <QObject>
#include <QVector3D>
#include <vector>

/// @brief 测量工具的逻辑与核心状态管理 (SRP)
///
/// 遵循 SRP 原则，该类仅负责点数据的管理、测距状态控制和距离计算，
/// 不涉及任何硬件或图形 API，保证了纯净的业务模型和极强的可测试性。
class MeasureToolController : public QObject {
  Q_OBJECT
 public:
  explicit MeasureToolController(QObject* parent = nullptr);
  ~MeasureToolController() override;

  /// @brief 获取当前工具激活状态
  bool IsActive() const { return active_; }

  /// @brief 激活或停用测量模式
  void SetActive(bool active);

  /// @brief 添加一个新的测量点，并在发生变化时发射信号
  void AddPoint(const QVector3D& world_pos);

  /// @brief 清除所有已被选取的测量点
  void ClearPoints();

  /// @brief 返回已被选取的全部测量点
  const std::vector<QVector3D>& Points() const { return points_; }

  /// @brief 累加计算当前所有测量点线段总长度
  double TotalDistance() const;

 signals:
  void activeChanged(bool active);
  void TotalDistanceChanged(double distance);
  void pointsChanged();

 private:
  bool active_ = false;
  std::vector<QVector3D> points_;
};
