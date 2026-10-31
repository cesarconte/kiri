set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_definitions(-DTRANSLATION_DOMAIN=\"plasma_applet_org.cesar.kiri\")

ecm_qt_declare_logging_category(kiri_SRCS
    HEADER debug.h
    IDENTIFIER KIRI_LOG
    CATEGORY_NAME org.cesar.kiri
    DEFAULT_SEVERITY Info
    DESCRIPTION "Kiri voice and AI assistant"
    EXPORT KIRI
)

plasma_add_applet(org.cesar.kiri
    CPP_SOURCES
        kiriapplet.cpp
        ${kiri_SRCS}
    QML_SOURCES
        qml/main.qml
        qml/ExpandedRepresentation.qml
        qml/SiriOrbAnimation.qml
        qml/config.qml
        qml/ConfigGeneral.qml
    RESOURCES
        res/main.xml
        res/org.cesar.kiri.svg
        res/kiri.svg
        res/kiri-symbolic.svg
        res/kiri-listening.svg
        res/kiri-listening-symbolic.svg
)

target_include_directories(org.cesar.kiri PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(org.cesar.kiri PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Quick
    Qt6::Widgets
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    KF6::Notifications
    KF6::Service
    KF6::WindowSystem
    Plasma::Plasma
    Plasma::PlasmaQuick
)

# Install icons to standard hicolor icon directories
install(FILES res/org.cesar.kiri.svg
              res/kiri.svg
              res/kiri-symbolic.svg
              res/kiri-listening.svg
              res/kiri-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)
