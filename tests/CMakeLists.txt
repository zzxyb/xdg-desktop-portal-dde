find_package(Qt6 REQUIRED COMPONENTS Core DBus Widgets)

add_executable(test-settings-dbus-metatype
    test_settings_dbus_metatype.cpp
    ${CMAKE_SOURCE_DIR}/src/settings.cpp
    ${CMAKE_SOURCE_DIR}/src/settings.h
)

target_include_directories(test-settings-dbus-metatype PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test-settings-dbus-metatype PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
)

add_test(NAME settings-dbus-metatype COMMAND test-settings-dbus-metatype)
set_tests_properties(settings-dbus-metatype PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test-screencast-options
    test_screencast_options.cpp
)

target_include_directories(test-screencast-options PRIVATE
    ${CMAKE_SOURCE_DIR}/src/wayland
)

target_link_libraries(test-screencast-options PRIVATE
    xdg-desktop-portal-dde-wayland
)

add_test(NAME screencast-options COMMAND test-screencast-options)
set_tests_properties(screencast-options PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
