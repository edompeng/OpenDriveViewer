#pragma once

#include <QtCore/qglobal.h>

#if defined(GEOVIEWER_STATIC)
#  define GEOVIEWER_EXPORT
#else
#  if defined(GEOVIEWER_BUILD_LIB)
#    define GEOVIEWER_EXPORT Q_DECL_EXPORT
#  else
#    define GEOVIEWER_EXPORT Q_DECL_IMPORT
#  endif
#endif
