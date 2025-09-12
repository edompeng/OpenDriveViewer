#include "src/utility/coordinate_util.h"

#include <iostream>
#include <stdexcept>

CoordinateUtil::~CoordinateUtil() {
  if (xodr_pj_) {
    proj_destroy(xodr_pj_);
    xodr_pj_ = nullptr;
  }
  if (xodr_proj_ctx_) {
    proj_context_destroy(xodr_proj_ctx_);
    xodr_proj_ctx_ = nullptr;
  }
}

CoordinateUtil& CoordinateUtil::Instance() {
  static CoordinateUtil util;
  return util;
}

void CoordinateUtil::Init(const std::string& georeference,
                          const double& x_offset, const double& y_offset) {
  if (xodr_pj_) {
    proj_destroy(xodr_pj_);
    xodr_pj_ = nullptr;
  }
  if (xodr_proj_ctx_) {
    proj_context_destroy(xodr_proj_ctx_);
    xodr_proj_ctx_ = nullptr;
  }

  xodr_proj_ctx_ = proj_context_create();
  if (xodr_proj_ctx_ == nullptr) {
    throw std::runtime_error("Failed to create PROJ context");
  }

  static const std::string kToProj = "+proj=longlat +datum=WGS84 +no_defs";

  xodr_pj_ = proj_create_crs_to_crs(xodr_proj_ctx_, georeference.c_str(),
                                    kToProj.c_str(), nullptr);
  if (xodr_pj_ == nullptr) {
    std::string error = "Failed to create PROJ transformation: ";
    error += proj_errno_string(proj_context_errno(xodr_proj_ctx_));
    throw std::runtime_error(error);
  }

  xodr_pj_ = proj_normalize_for_visualization(xodr_proj_ctx_, xodr_pj_);
  x_offset_ = x_offset;
  y_offset_ = y_offset;

  const PJ_PROJ_INFO info = proj_pj_info(xodr_pj_);
  std::clog << "Projection description: " << info.description << '\n';
}

void CoordinateUtil::WGS84ToLocal(double* const x, double* const y,
                                  double* const z) {
  if (xodr_pj_ == nullptr) {
    throw std::runtime_error("PROJ transformation not initialized");
  }

  PJ_COORD a;
  a.lpz.lam = *x;
  a.lpz.phi = *y;
  a.lpz.z = z ? *z : 0.0;

  const PJ_COORD b = proj_trans(xodr_pj_, PJ_INV, a);

  *x = b.xyz.x - x_offset_;
  *y = b.xyz.y - y_offset_;
  if (z) {
    *z = b.xyz.z;
  }
}

void CoordinateUtil::LocalToWGS84(double* const x, double* const y,
                                  double* const z) {
  if (xodr_pj_ == nullptr) {
    throw std::runtime_error("PROJ transformation not initialized");
  }

  PJ_COORD a;
  a.xyz.x = *x + x_offset_;
  a.xyz.y = *y + y_offset_;
  a.xyz.z = z ? *z : 0.0;

  const PJ_COORD b = proj_trans(xodr_pj_, PJ_FWD, a);

  *x = b.lpz.lam;
  *y = b.lpz.phi;
  if (z) {
    *z = b.lpz.z;
  }
}
