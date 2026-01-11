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

add_executable(NMSSaveExplorer-Qt ${NMS_SOURCES})

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
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/ DESTINATION resources)

set(CPACK_PACKAGE_NAME "NMSSaveExplorer-Qt")
set(CPACK_PACKAGE_VENDOR "NMSSaveExplorer-Qt")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_GENERATOR "ZIP")
if(APPLE)
  list(APPEND CPACK_GENERATOR "DragNDrop")
endif()
include(CPack)

if(WIN32)
  add_custom_target(package_windows_zip
    DEPENDS NMSSaveExplorer-Qt
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
  add_custom_target(package_linux_appimage
    DEPENDS NMSSaveExplorer-Qt
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/AppDir
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG> --prefix ${CMAKE_BINARY_DIR}/AppDir/usr
    COMMAND linuxdeployqt ${CMAKE_BINARY_DIR}/AppDir/usr/bin/NMSSaveExplorer-Qt -appimage
    COMMENT "Creating Linux AppImage"
  )
endif()

qt_finalize_executable(NMSSaveExplorer-Qt)
