#pragma once

#include <QtCore/qglobal.h>

#if defined(GEOVIEWER_CORE_LIB)
#  define GEOVIEWER_CORE_EXPORT Q_DECL_EXPORT
#else
#  define GEOVIEWER_CORE_EXPORT Q_DECL_IMPORT
#endif
