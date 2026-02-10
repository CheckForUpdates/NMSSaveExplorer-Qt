cmake_minimum_required(VERSION 3.16)
project(NMSSaveExplorer-Qt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Widgets Xml Concurrent REQUIRED)
set(QT_PACKAGE Qt6)

file(GLOB_RECURSE NMS_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lz4/lz4.c
)

file(GLOB NMS_RES_ROOT CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/*.json
)
file(GLOB_RECURSE NMS_RES_DATA CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/data/*
)
file(GLOB_RECURSE NMS_RES_ICONS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/icons/*
)

if(WIN32)
  add_executable(NMSSaveExplorer-Qt WIN32 ${NMS_SOURCES})
else()
  add_executable(NMSSaveExplorer-Qt ${NMS_SOURCES})
endif()

add_executable(ItemCatalogBuilder
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/ItemCatalogBuilder.cpp
)
target_link_libraries(ItemCatalogBuilder PRIVATE ${QT_PACKAGE}::Core ${QT_PACKAGE}::Xml)


set(ITEM_CATALOG_OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/item_catalog.json)
add_custom_command(
  OUTPUT ${ITEM_CATALOG_OUTPUT}
  COMMAND $<TARGET_FILE:ItemCatalogBuilder>
          --resources ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
          --output ${ITEM_CATALOG_OUTPUT}
  DEPENDS ItemCatalogBuilder
          ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/data/nms_reality_gcproducttable.MXML
          ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/data/nms_basepartproducts.MXML
          ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/data/nms_reality_gcsubstancetable.MXML
          ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/data/nms_reality_gctechnologytable.MXML
          ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/localisation_map.json
  COMMENT "Generating cached item catalog"
)
add_custom_target(generate_item_catalog DEPENDS ${ITEM_CATALOG_OUTPUT})
add_dependencies(NMSSaveExplorer-Qt generate_item_catalog)

target_include_directories(NMSSaveExplorer-Qt PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lz4
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidjson/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(NMSSaveExplorer-Qt PRIVATE ${QT_PACKAGE}::Widgets ${QT_PACKAGE}::Xml ${QT_PACKAGE}::Concurrent)

if(APPLE)
  set_target_properties(NMSSaveExplorer-Qt PROPERTIES MACOSX_BUNDLE ON)
endif()

include(GNUInstallDirs)
install(TARGETS NMSSaveExplorer-Qt
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)
option(NMS_RESOURCE_LIBS "Build resources into shared libraries" OFF)
if(WIN32)
  set(NMS_RESOURCE_LIBS ON CACHE BOOL "Build resources into shared libraries" FORCE)
endif()
if(NOT NMS_RESOURCE_LIBS)
  if(NOT WIN32)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/ DESTINATION resources)
  endif()
endif()

set(CPACK_PACKAGE_NAME "NMSSaveExplorer-Qt")
set(CPACK_PACKAGE_VENDOR "NMSSaveExplorer-Qt")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_GENERATOR "ZIP")
if(APPLE)
  list(APPEND CPACK_GENERATOR "DragNDrop")
endif()
include(CPack)

set(NMS_RESOURCE_DEPS)
if(NMS_RESOURCE_LIBS)
  add_library(NMSResourcesRoot SHARED)
  qt_add_resources(NMSResourcesRoot nms_resources_root
    PREFIX "/resources"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
    FILES ${NMS_RES_ROOT}
  )
  target_link_libraries(NMSResourcesRoot PRIVATE ${QT_PACKAGE}::Core)
  add_dependencies(NMSResourcesRoot generate_item_catalog)

  add_library(NMSResourcesData SHARED)
  qt_add_resources(NMSResourcesData nms_resources_data
    PREFIX "/resources"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
    FILES ${NMS_RES_DATA}
  )
  target_link_libraries(NMSResourcesData PRIVATE ${QT_PACKAGE}::Core)

  set(NMS_ICONS_QRC ${CMAKE_CURRENT_BINARY_DIR}/resources_icons.qrc)
  set(NMS_ICONS_RCC ${CMAKE_CURRENT_BINARY_DIR}/resources_icons.rcc)
  file(WRITE ${NMS_ICONS_QRC} "<!DOCTYPE RCC><RCC version=\"1.0\">\n<qresource prefix=\"/resources/icons\">\n")
  foreach(icon_file ${NMS_RES_ICONS})
    file(RELATIVE_PATH icon_rel ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/icons ${icon_file})
    string(REPLACE "\\" "/" icon_rel "${icon_rel}")
    file(REAL_PATH "${icon_file}" icon_abs)
    string(REPLACE "\\" "/" icon_abs "${icon_abs}")
    file(APPEND ${NMS_ICONS_QRC} "  <file alias=\"${icon_rel}\">${icon_abs}</file>\n")
  endforeach()
  file(APPEND ${NMS_ICONS_QRC} "</qresource>\n</RCC>\n")
  add_custom_command(
    OUTPUT ${NMS_ICONS_RCC}
    COMMAND $<TARGET_FILE:Qt6::rcc> -binary -o ${NMS_ICONS_RCC} ${NMS_ICONS_QRC}
    DEPENDS ${NMS_ICONS_QRC} ${NMS_RES_ICONS}
    COMMENT "Building icons RCC"
  )
  add_custom_target(generate_icons_rcc DEPENDS ${NMS_ICONS_RCC})
  add_custom_target(clean_icons_rcc
    COMMAND ${CMAKE_COMMAND} -E rm -f ${NMS_ICONS_QRC} ${NMS_ICONS_RCC}
    COMMENT "Removing generated icons QRC/RCC"
  )
  add_dependencies(NMSSaveExplorer-Qt generate_icons_rcc)

  install(TARGETS NMSResourcesRoot NMSResourcesData
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES ${NMS_ICONS_RCC} DESTINATION ${CMAKE_INSTALL_BINDIR})

  set(NMS_RESOURCE_DEPS NMSResourcesRoot NMSResourcesData generate_icons_rcc)
endif()

if(WIN32)
  add_custom_target(package_windows_zip
    DEPENDS NMSSaveExplorer-Qt ${NMS_RESOURCE_DEPS}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/package
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG> --prefix ${CMAKE_BINARY_DIR}/package
    COMMAND windeployqt ${CMAKE_BINARY_DIR}/package/bin/NMSSaveExplorer-Qt.exe
    COMMAND ${CMAKE_COMMAND} -E tar "cfv" ${CMAKE_BINARY_DIR}/NMSSaveExplorer-Qt-windows.zip --format=zip ${CMAKE_BINARY_DIR}/package
    COMMENT "Creating portable Windows zip"
  )
endif()

if(APPLE)
  add_custom_target(package_macos_dmg
    DEPENDS NMSSaveExplorer-Qt
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/package
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG> --prefix ${CMAKE_BINARY_DIR}/package
    COMMAND macdeployqt ${CMAKE_BINARY_DIR}/package/NMSSaveExplorer-Qt.app -dmg
    COMMENT "Creating macOS dmg"
  )
endif()

if(UNIX AND NOT APPLE)
  if(CMAKE_CONFIGURATION_TYPES)
    set(NMS_INSTALL_CONFIG_ARG --config $<CONFIG>)
  else()
    set(NMS_INSTALL_CONFIG_ARG)
  endif()
  add_custom_target(package_linux_appimage
    DEPENDS NMSSaveExplorer-Qt ${NMS_RESOURCE_DEPS}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/AppDir
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} ${NMS_INSTALL_CONFIG_ARG} --prefix ${CMAKE_BINARY_DIR}/AppDir/usr
    COMMAND linuxdeployqt ${CMAKE_BINARY_DIR}/AppDir/usr/bin/NMSSaveExplorer-Qt -appimage
    COMMENT "Creating Linux AppImage"
  )
endif()

qt_finalize_executable(NMSSaveExplorer-Qt)
