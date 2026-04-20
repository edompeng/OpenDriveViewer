#pragma once

#include <QtCore/qglobal.h>

#if defined(GEOVIEWER_LOGIC_LIB)
#  define GEOVIEWER_LOGIC_EXPORT Q_DECL_EXPORT
#else
#  define GEOVIEWER_LOGIC_EXPORT Q_DECL_IMPORT
#endif
