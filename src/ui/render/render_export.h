#pragma once

#include <QtCore/qglobal.h>

#if defined(GEOVIEWER_RENDER_LIB)
#  define GEOVIEWER_RENDER_EXPORT Q_DECL_EXPORT
#else
#  define GEOVIEWER_RENDER_EXPORT Q_DECL_IMPORT
#endif
