########################################################
# Files
find_package(Qt5 COMPONENTS OpenGL REQUIRED)

set(CMAKE_AUTOMOC ON)
set (parammodeler_SRCS
  parammodeler.cpp
  parammodeler_dock.cpp
  exportjson.cpp
  exportobj.cpp  
		exportpointcloud.cpp
		buildmesh.cpp
  previewglwidget.cpp
		
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
  qgis_core
  qgis_gui
		Qt5::OpenGL
  opengl32
		qgis_3d
)
########################################################
# Install
install(TARGETS plugin_parammodeler
  RUNTIME DESTINATION ${QGIS_PLUGIN_DIR}
  LIBRARY DESTINATION ${QGIS_PLUGIN_DIR})
