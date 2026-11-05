set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_definitions(-DTRANSLATION_DOMAIN=\"plasma_applet_org.cesar.kity\")

ecm_qt_declare_logging_category(kity_SRCS
    HEADER debug.h
    IDENTIFIER KITY_LOG
    CATEGORY_NAME org.cesar.kity
    DEFAULT_SEVERITY Info
    DESCRIPTION "Kity voice and AI assistant"
    EXPORT KITY
)

plasma_add_applet(org.cesar.kity
    CPP_SOURCES
        kityapplet.cpp
        mathnotes.cpp
        mathnotes.h
        ${kity_SRCS}
    QML_SOURCES
        qml/main.qml
        qml/ExpandedRepresentation.qml
        qml/SiriOrbAnimation.qml
        qml/config.qml
        qml/ConfigGeneral.qml
    RESOURCES
        res/main.xml
        res/org.cesar.kity.svg
        res/kity.svg
        res/kity-symbolic.svg
        res/kity-listening.svg
        res/kity-listening-symbolic.svg
)

target_include_directories(org.cesar.kity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(org.cesar.kity PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Quick
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    KF6::Notifications
    KF6::Service
    Plasma::Plasma
    Plasma::PlasmaQuick
)

# Install icons to standard hicolor icon directories
install(FILES res/org.cesar.kity.svg
              res/kity.svg
              res/kity-symbolic.svg
              res/kity-listening.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)

# Spotlight / KRunner Plugin (Phase 4)
kcoreaddons_add_plugin(krunner_kity
    INSTALL_NAMESPACE "kf6/krunner"
    SOURCES
        runner/kityrunner.cpp
        runner/kityrunner.h
)

target_link_libraries(krunner_kity PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    KF6::CoreAddons
    KF6::I18n
    KF6::Runner
)

# Standalone Kity Desktop Application (macOS Golden Gate Siri App Style)
add_executable(kity
    app/main.cpp
    app/kityclient.cpp
    app/kityclient.h
    app/resources.qrc
)

target_include_directories(kity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)

target_link_libraries(kity PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Quick
    Qt6::Widgets
    KF6::CoreAddons
    KF6::DBusAddons
    KF6::I18n
    KF6::WindowSystem
)

install(TARGETS kity ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES org.cesar.kity.desktop
        DESTINATION ${KDE_INSTALL_APPDIR}
)
