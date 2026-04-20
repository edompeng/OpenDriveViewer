#pragma once

#include <proj.h>
#include <string>
#include "src/core/core_export.h"

class GEOVIEWER_CORE_EXPORT CoordinateUtil {
 public:
  static CoordinateUtil& Instance();
  ~CoordinateUtil();
  void Init(const std::string& georeference, const double& x_offset,
            const double& y_offset);

  void WGS84ToLocal(double* const x, double* const y, double* const z);
  void LocalToWGS84(double* const x, double* const y, double* const z);

 private:
  CoordinateUtil() = default;
  CoordinateUtil(const CoordinateUtil&) = delete;
  CoordinateUtil(CoordinateUtil&&) = delete;
  CoordinateUtil& operator=(const CoordinateUtil&) = delete;
  CoordinateUtil& operator=(CoordinateUtil&&) = delete;

  PJ_CONTEXT* xodr_proj_ctx_ = nullptr;
  PJ* xodr_pj_ = nullptr;
  double x_offset_ = 0.0;
  double y_offset_ = 0.0;
};
