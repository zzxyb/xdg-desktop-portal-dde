set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_get_variable(WlrProtocols_PKGDATADIR wlr-protocols pkgdatadir)
pkg_get_variable(WAYLANDPROTOCOlS_PKGDATADIR wayland-protocols pkgdatadir)
find_package(Qt6 COMPONENTS REQUIRED Core DBus WaylandClient WaylandScannerTools Quick)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
  find_package(Qt6 COMPONENTS GuiPrivate WaylandClientPrivate REQUIRED)
endif()
find_package(Dtk6 COMPONENTS Core Gui Declarative REQUIRED)
find_package(TreelandProtocols REQUIRED)
find_package(Libdrm REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)
pkg_check_modules(GBM IMPORTED_TARGET gbm REQUIRED)
pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3 REQUIRED)
pkg_check_modules(EGL IMPORTED_TARGET egl REQUIRED)
pkg_check_modules(GLES2 IMPORTED_TARGET glesv2 REQUIRED)
pkg_check_modules(VULKAN IMPORTED_TARGET vulkan REQUIRED)

add_library(xdg-desktop-portal-dde-wayland SHARED
    loggings.h
    loggings.cpp
    portalwaylandcontext.h
    portalwaylandcontext.cpp
    screenshotportal.h
    screenshotportal.cpp
    abstractwaylandportal.h
    treelandintegration.h
    treelandintegration.cpp
    dbushelpers.h
    dbushelpers.cpp
    session.h
    session.cpp
    screencastsession.h
    screencastsession.cpp
    fpslimit.h
    fpslimit.c
    pipewireutils.h
    pipewireutils.cpp
    pipewirecore.h
    pipewirecore.cpp
    screencastportal.h
    screencastportal.cpp
    screencastcontext.h
    screencastcontext.cpp
    portalcommon.h
    remotedesktopsession.h
    remotedesktopsession.cpp
    screencastchooser.h
    screencastchooser.cpp
    screenlistmodel.h
    screenlistmodel.cpp
    protocols/screencopy.h
    protocols/screencopy.cpp
    protocols/common.h
    protocols/treelandcapture.h
    protocols/treelandcapture.cpp
    protocols/shmbuffer.h
    protocols/shmbuffer.cpp
    protocols/linuxdmabufer.h
    protocols/linuxdmabufer.cpp
    request2.h
    request2.cpp
    restoredata.h
    restoredata.cpp
    protocols/foreigntoplevellist.h
    protocols/foreigntoplevellist.cpp
    protocols/imagecopycapture.h
    protocols/imagecopycapture.cpp
    protocols/imagecapturesource.h
    protocols/imagecapturesource.cpp
    toplevelmodel.h
    toplevelmodel.cpp
    amhelper.h
    amhelper.cpp
    abstractpipewirestream.h
    abstractpipewirestream.cpp
    screencastpreview.h
    screencastpreview.cpp
    screencastpreviewitem.h
    screencastpreviewitem.cpp
    screencastpreviewpainteditem.h
    screencastpreviewpainteditem.cpp
    outputpipewirestream.h
    outputpipewirestream.cpp
    toplevelpipewirestream.h
    toplevelpipewirestream.cpp
    protocols/wallpapermanagerclient.h
    protocols/wallpapermanagerclient.cpp
    ../wallpaper.cpp
    ../utils.cpp
)

qt6_generate_wayland_protocol_client_sources(xdg-desktop-portal-dde-wayland FILES
    ${WlrProtocols_PKGDATADIR}/unstable/wlr-screencopy-unstable-v1.xml
    ${TREELAND_PROTOCOLS_DATA_DIR}/treeland-capture-unstable-v1.xml
    /usr/share/wayland/wayland.xml
    ${WAYLANDPROTOCOlS_PKGDATADIR}/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
    ${WAYLANDPROTOCOlS_PKGDATADIR}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
    ${WAYLANDPROTOCOlS_PKGDATADIR}/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
    ${WAYLANDPROTOCOlS_PKGDATADIR}/staging/ext-image-capture-source/ext-image-capture-source-v1.xml
    ${TREELAND_PROTOCOLS_DATA_DIR}/treeland-wallpaper-manager-unstable-v1.xml
)

# Patch the generated linux-dmabuf protocol header to include core wayland interface
set(DMABUF_HEADER ${CMAKE_CURRENT_BINARY_DIR}/wayland-linux-dmabuf-unstable-v1-client-protocol.h)
set(PATCH_SCRIPT ${CMAKE_SOURCE_DIR}/patch_dmabuf_header.py)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Create a custom target to patch the header after it's generated but before compilation
# This solves the problem where wl_buffer_interface is not defined by prepending the core wayland include
add_custom_command(
    OUTPUT ${DMABUF_HEADER}.patched
    DEPENDS ${DMABUF_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${PATCH_SCRIPT} ${DMABUF_HEADER}
    COMMAND ${CMAKE_COMMAND} -E touch ${DMABUF_HEADER}.patched
    COMMENT "Prepending wayland core protocol include to linux-dmabuf header"
)

# Make the library depend on the patched header
add_custom_target(patch_dmabuf_header DEPENDS ${DMABUF_HEADER}.patched)
add_dependencies(xdg-desktop-portal-dde-wayland patch_dmabuf_header)

target_include_directories(xdg-desktop-portal-dde-wayland
PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

qt_add_qml_module(xdg-desktop-portal-dde-wayland
    URI screencast
    VERSION 1.0
    QML_FILES
        ScreencastChooserWindow.qml
        ScreenCastPreviewView.qml
        OutputListView.qml
        ToplevelList.qml
        Background.qml
)

find_program(QSB_EXECUTABLE NAMES qsb PATHS /usr/lib/qt6/bin REQUIRED)

set(SCREENCAST_PREVIEW_VERT_QSB ${CMAKE_CURRENT_BINARY_DIR}/preview.vert.qsb)
set(SCREENCAST_PREVIEW_FRAG_QSB ${CMAKE_CURRENT_BINARY_DIR}/preview.frag.qsb)

add_custom_command(
    OUTPUT ${SCREENCAST_PREVIEW_VERT_QSB}
    COMMAND ${QSB_EXECUTABLE} --qt6 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/preview.vert -o ${SCREENCAST_PREVIEW_VERT_QSB}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/preview.vert
    COMMENT "Compiling preview vertex shader"
)

add_custom_command(
    OUTPUT ${SCREENCAST_PREVIEW_FRAG_QSB}
    COMMAND ${QSB_EXECUTABLE} --qt6 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/preview.frag -o ${SCREENCAST_PREVIEW_FRAG_QSB}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/preview.frag
    COMMENT "Compiling preview fragment shader"
)

set_source_files_properties(${SCREENCAST_PREVIEW_VERT_QSB} PROPERTIES QT_RESOURCE_ALIAS "preview.vert.qsb")
set_source_files_properties(${SCREENCAST_PREVIEW_FRAG_QSB} PROPERTIES QT_RESOURCE_ALIAS "preview.frag.qsb")

qt_add_resources(xdg-desktop-portal-dde-wayland "screencast_preview_shaders"
    PREFIX
        "/screencast/shaders"
    FILES
        ${SCREENCAST_PREVIEW_VERT_QSB}
        ${SCREENCAST_PREVIEW_FRAG_QSB}
)

target_link_libraries(xdg-desktop-portal-dde-wayland
PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::GuiPrivate
    Qt6::WaylandClientPrivate
    Qt6::Quick
    Dtk6::Core
    Dtk6::Gui
    Dtk6::Declarative
    Libdrm::Libdrm
    PkgConfig::GBM
    PkgConfig::PIPEWIRE
    PkgConfig::EGL
    PkgConfig::GLES2
    PkgConfig::VULKAN
    Wayland::Client
)

install(TARGETS xdg-desktop-portal-dde-wayland DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES icons/portal-screencast.dci DESTINATION share/dsg/icons/)
