#include "src/core/coordinate_util.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <iostream>
#include <stdexcept>

namespace {
struct ProjThreadCache {
  PJ_CONTEXT* ctx = nullptr;
  PJ* pj = nullptr;
  std::string georeference;

  ~ProjThreadCache() {
    if (pj) proj_destroy(pj);
    if (ctx) proj_context_destroy(ctx);
  }

  bool Update(const std::string& new_georef) {
    if (new_georef.empty()) return false;
    if (georeference == new_georef && pj != nullptr) return true;

    if (pj) {
      proj_destroy(pj);
      pj = nullptr;
    }
    if (ctx) {
      proj_context_destroy(ctx);
      ctx = nullptr;
    }

    georeference = new_georef;
    ctx = proj_context_create();
    if (!ctx) return false;

#ifdef Q_OS_MAC
    QString appDir = QCoreApplication::applicationDirPath();
    QString projPath = QDir(appDir).filePath("../Resources/proj");
    if (QFile::exists(projPath)) {
      QByteArray utf8Path = projPath.toUtf8();
      const char* paths[] = {utf8Path.constData()};
      proj_context_set_search_paths(ctx, 1, paths);
    }
#endif

    static const std::string kToProj = "+proj=longlat +datum=WGS84 +no_defs";
    PJ* raw_pj = proj_create_crs_to_crs(ctx, georeference.c_str(),
                                        kToProj.c_str(), nullptr);
    if (!raw_pj) return false;

    pj = proj_normalize_for_visualization(ctx, raw_pj);
    proj_destroy(raw_pj);
    return true;
  }
};

thread_local ProjThreadCache g_proj_cache;
}  // namespace

CoordinateUtil::~CoordinateUtil() {}

CoordinateUtil& CoordinateUtil::Instance() {
  static CoordinateUtil util;
  return util;
}

void CoordinateUtil::Init(const std::string& georeference,
                          const double& x_offset, const double& y_offset) {
  std::lock_guard<std::mutex> lock(mutex_);
  georeference_ = georeference;
  x_offset_ = x_offset;
  y_offset_ = y_offset;
}

void CoordinateUtil::WGS84ToLocal(double* const x, double* const y,
                                  double* const z) {
  std::string georef;
  double ox, oy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    georef = georeference_;
    ox = x_offset_;
    oy = y_offset_;
  }

  if (!g_proj_cache.Update(georef)) {
    throw std::runtime_error("PROJ transformation not initialized or invalid");
  }

  PJ_COORD a;
  a.lpz.lam = *x;
  a.lpz.phi = *y;
  a.lpz.z = z ? *z : 0.0;

  const PJ_COORD b = proj_trans(g_proj_cache.pj, PJ_INV, a);
  if (b.xyz.x == HUGE_VAL) {
    throw std::runtime_error("WGS84 to Local conversion failed");
  }

  *x = b.xyz.x - ox;
  *y = b.xyz.y - oy;
  if (z) {
    *z = b.xyz.z;
  }
}

void CoordinateUtil::LocalToWGS84(double* const x, double* const y,
                                  double* const z) {
  std::string georef;
  double ox, oy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    georef = georeference_;
    ox = x_offset_;
    oy = y_offset_;
  }

  if (!g_proj_cache.Update(georef)) {
    throw std::runtime_error("PROJ transformation not initialized or invalid");
  }

  PJ_COORD a;
  a.xyz.x = *x + ox;
  a.xyz.y = *y + oy;
  a.xyz.z = z ? *z : 0.0;

  const PJ_COORD b = proj_trans(g_proj_cache.pj, PJ_FWD, a);
  if (b.lpz.lam == HUGE_VAL) {
    throw std::runtime_error("Local to WGS84 conversion failed");
  }

  *x = b.lpz.lam;
  *y = b.lpz.phi;
  if (z) {
    *z = b.lpz.z;
  }
}
