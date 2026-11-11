# Input context plugins use Qt's PRIVATE ABI. Never deploy an OS-provided
# fcitx plugin beside Conan's Qt: even a patch-level mismatch can fail dlopen.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()

find_package(Qt6 REQUIRED COMPONENTS DBus)
if(NOT TARGET Qt6::GuiPrivate OR NOT TARGET Qt6::qmake)
  message(FATAL_ERROR "Linux input methods require the private headers and qmake from Muffin's Qt package")
endif()
find_package(ECM 5.0 REQUIRED NO_MODULE)
include(FetchContent)

# A function keeps upstream's generic options, compiler settings and module
# search paths out of Muffin's directory scope. Use the official plugin-only
# build, including its D-Bus objects, without installing into Conan's cache.
function(muffin_build_fcitx)
  set(ENABLE_QT4 OFF)
  set(ENABLE_QT5 OFF)
  set(ENABLE_QT6 ON)
  set(ENABLE_X11 ON)
  set(BUILD_ONLY_PLUGIN ON)
  set(BUILD_STATIC_PLUGIN OFF)
  set(WITH_FCITX_PLUGIN_NAME ON)
  # The shipped Qt uses xcb (also on Wayland desktops through XWayland).
  # Native Wayland requires a separately enabled/tested QtWayland build.
  set(ENABLE_QT6_WAYLAND_WORKAROUND OFF)
  # Upstream discovers qmake's install path and caches it. Reserve its cache
  # entry first, so even an explicit install cannot write into the Qt package.
  set(CMAKE_INSTALL_QT6PLUGINDIR "${CMAKE_BINARY_DIR}/muffin-plugins"
    CACHE PATH "Staged Qt input context plugins" FORCE)
  FetchContent_Declare(muffin_fcitx5_qt
    URL https://codeload.github.com/fcitx/fcitx5-qt/tar.gz/refs/tags/5.1.16
    URL_HASH SHA256=94883b1f5501b9e8e994ce1790932750457d284dae992603cb40675c00eedea5
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_MakeAvailable(muffin_fcitx5_qt)
  set_target_properties(fcitx5platforminputcontextplugin-qt6 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/muffin-plugins/platforminputcontexts")
  set(MUFFIN_FCITX_LICENSE_DIR "${muffin_fcitx5_qt_SOURCE_DIR}/LICENSES" PARENT_SCOPE)
  set(MUFFIN_FCITX_README "${muffin_fcitx5_qt_SOURCE_DIR}/README.md" PARENT_SCOPE)
endfunction()

muffin_build_fcitx()
set(MUFFIN_FCITX_PLUGIN "$<TARGET_FILE:fcitx5platforminputcontextplugin-qt6>")
