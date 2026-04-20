if(MSVC)
  add_compile_options(/WX)
else()
  add_compile_options(-Werror)
endif()

add_definitions(-DGEOVIEWER_BUILD_LIB)

add_subdirectory(core)
add_subdirectory(logic)
add_subdirectory(app)
add_subdirectory(ui)
