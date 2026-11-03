cmake_minimum_required(VERSION 3.25)
project(FluidTiles VERSION 0.5.3 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus)
include(KDEInstallDirs6)
find_package(KWin 6.8 REQUIRED)
find_package(KF6GlobalAccel REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6KCMUtils REQUIRED)
find_package(KF6ColorScheme REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KDecoration3 REQUIRED)
find_package(KWayland REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)

add_library(fluidtiles MODULE src/effect.cpp src/effect.h src/core.h src/settings.h src/appearance.h src/appearance.cpp)
set_property(TARGET fluidtiles APPEND PROPERTY AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED)
target_link_libraries(fluidtiles PRIVATE KWin::kwin Qt6::DBus KF6::GlobalAccel KF6::ConfigCore KF6::ColorScheme KDecoration3::KDecoration ${CMAKE_DL_LIBS})
target_compile_options(fluidtiles PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(fluidtiles PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/kwin/effects/plugins")
install(TARGETS fluidtiles DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)

add_library(fluidbootstrap MODULE src/bootstrap.cpp)
target_link_libraries(fluidbootstrap PRIVATE KWin::kwin Qt6::Qml)
set_target_properties(fluidbootstrap PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bootstrap")
add_executable(fluidtiles-settings tools/settings.cpp)
target_link_libraries(fluidtiles-settings PRIVATE Qt6::Widgets KF6::KCMUtils KF6::CoreAddons)
add_custom_target(fluidtiles-package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/package/contents/native/kwin/effects/plugins" "${CMAKE_BINARY_DIR}/package/contents/ui/bootstrap"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/package" "${CMAKE_BINARY_DIR}/package"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:fluidtiles>" "${CMAKE_BINARY_DIR}/package/contents/native/kwin/effects/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:fluidbootstrap>" "${CMAKE_BINARY_DIR}/package/contents/ui/bootstrap/"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:fluidtiles-settings>" "${CMAKE_BINARY_DIR}/package/contents/native/"
    DEPENDS fluidtiles fluidbootstrap fluidtiles-settings)

add_executable(fluidtiles-demo tools/demo.cpp)
target_link_libraries(fluidtiles-demo PRIVATE Qt6::Widgets)
add_executable(fluidtiles-capture tools/capture.cpp)
target_link_libraries(fluidtiles-capture PRIVATE Qt6::Gui Qt6::DBus)
add_executable(fluidtiles-input tools/input.cpp)
target_link_libraries(fluidtiles-input PRIVATE Plasma::KWaylandClient Wayland::Client)
add_executable(fluidtiles-core-tests tests/core_test.cpp)
target_include_directories(fluidtiles-core-tests PRIVATE src)
target_link_libraries(fluidtiles-core-tests PRIVATE Qt6::Core)
enable_testing()
add_test(NAME core COMMAND fluidtiles-core-tests)

add_executable(fluidtiles-settings-tests tests/settings_test.cpp)
target_link_libraries(fluidtiles-settings-tests PRIVATE Qt6::Widgets KF6::KCMUtils KF6::ConfigCore KF6::CoreAddons KF6::WidgetsAddons)

target_include_directories(fluidtiles-settings-tests PRIVATE src)

add_executable(fluidtiles-imagecheck tools/imagecheck.cpp)
target_link_libraries(fluidtiles-imagecheck PRIVATE Qt6::Gui)
