# New Modular Structure
add_subdirectory(core)
add_subdirectory(renderer)
add_subdirectory(logic)
add_subdirectory(ui)

# Keep legacy for now to allow incremental migration
add_subdirectory(utility)
add_subdirectory(app)

set(GEOVIEWER_APP_TEST_SOURCES ${GEOVIEWER_APP_TEST_SOURCES} PARENT_SCOPE)
