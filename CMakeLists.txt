########################################################
# Files
find_package(Qt5 COMPONENTS OpenGL 3DCore 3DRender 3DExtras REQUIRED)
# Visual Studio regeneration may run without an activated conda environment.
# Prefer an explicit Eigen3_DIR, with the configured Qt prefix as a search hint.
get_filename_component(_parammodeler_qt_prefix "${Qt5_DIR}/../../.." ABSOLUTE)
find_package(Eigen3 3.3 REQUIRED NO_MODULE
  HINTS "${_parammodeler_qt_prefix}/share/eigen3/cmake"
)
unset(_parammodeler_qt_prefix)

set(CMAKE_AUTOMOC ON)
set (parammodeler_SRCS
  parammodeler.cpp
  parammodeler_dock.cpp
  parammodeler_datasetgen.cpp
  parammodeler_dlutils.cpp
  parammodeler_export.cpp
  parammodeler_params.cpp
  parammodeler_randomizer.cpp
  exportpointcloud.cpp
  buildmesh.cpp
  parammodeler_pcdloader.cpp
  parammodeler_pointnet.cpp
  parammodeler_config.cpp

  parammodeler_scene3d.cpp
  parammodeler_pick3d.cpp
)
set (parammodeler_UIS
  parammodeler_dock.ui
)
set(parammodeler_RCC parammodeler.qrc)
########################################################
# Build
qt_wrap_ui(parammodeler_UIS_H  ${parammodeler_UIS})
qt_add_resources(parammodeler_RCC parammodeler.qrc)
add_library(plugin_parammodeler MODULE
  ${parammodeler_SRCS}
  ${parammodeler_UIS_H}
  ${parammodeler_RCC}
)
# require c++17
target_compile_features(plugin_parammodeler PRIVATE cxx_std_17)
include_directories(
  ${CMAKE_SOURCE_DIR}/src/plugins
  ${CMAKE_SOURCE_DIR}/src/plugins/parammodeler
  ${CMAKE_BINARY_DIR}/src/ui
  ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(plugin_parammodeler
  Eigen3::Eigen
  qgis_core
  qgis_gui
  Qt5::OpenGL
  Qt5::3DCore
  Qt5::3DRender
  Qt5::3DExtras
  opengl32
  qgis_3d
)
########################################################
# Install
install(TARGETS plugin_parammodeler
  RUNTIME DESTINATION ${QGIS_PLUGIN_DIR}
  LIBRARY DESTINATION ${QGIS_PLUGIN_DIR})
