#pragma once

#include <string>
#include "src/geo_viewer_export.h"

namespace geoviewer::core {

class GEOVIEWER_EXPORT CrashHandler {
public:
    /**
     * @brief Initializes the crash/coredump collector handler.
     * @param dump_dir The local directory where coredump and crash log files will be saved.
     */
    static void Initialize(const std::string& dump_dir);

private:
    CrashHandler() = delete;
};

} // namespace geoviewer::core
