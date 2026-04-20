#pragma once

#include <QtCore/qglobal.h>

#if defined(GEOVIEWER_WIDGETS_LIB)
#  define GEOVIEWER_WIDGETS_EXPORT Q_DECL_EXPORT
#else
#  define GEOVIEWER_WIDGETS_EXPORT Q_DECL_IMPORT
#endif
