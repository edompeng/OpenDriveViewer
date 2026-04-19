add_subdirectory(widgets)

add_library(MainWindow SHARED
  main_window.cpp main_window.h
)

target_link_libraries(MainWindow PUBLIC 
    GeoViewerUIWidgets
)
