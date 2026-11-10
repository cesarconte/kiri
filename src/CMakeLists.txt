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
        mobile/mobileserver.cpp
        mobile/mobileserver.h
        ${kity_SRCS}
    QML_SOURCES
        qml/main.qml
        qml/SiriOrbAnimation.qml
        qml/cards/WeatherCard.qml
        qml/cards/MediaCard.qml
        qml/cards/EventCard.qml
    RESOURCES
        res/main.xml
        res/org.cesar.kity.svg
        res/kity.svg
        res/kity-symbolic.svg
        res/kity-listening.svg
        res/kity-listening-symbolic.svg
        mobile/mobile_resources.qrc
)

target_include_directories(org.cesar.kity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mobile
)

target_link_libraries(org.cesar.kity PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Quick
    Qt6::WebSockets
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

install(FILES res/kity-symbolic.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/symbolic/apps
)

install(FILES res/kity-symbolic.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/status
)

# Also ensure macOS-GoldenGate theme variants receive the symbolic and full-color icons
install(CODE "
    set(THEME_NAMES macOS-GoldenGate macOS-GoldenGate-dark macOS-GoldenGate-light)
    foreach(theme \${THEME_NAMES})
        set(THEME_DIR \"\$ENV{HOME}/.local/share/icons/\${theme}\")
        if(IS_DIRECTORY \"\${THEME_DIR}\")
            file(MAKE_DIRECTORY \"\${THEME_DIR}/apps/symbolic\" \"\${THEME_DIR}/status/symbolic\" \"\${THEME_DIR}/apps/scalable\")
            file(COPY \"${CMAKE_CURRENT_SOURCE_DIR}/res/kity-symbolic.svg\" DESTINATION \"\${THEME_DIR}/apps/symbolic\")
            file(COPY \"${CMAKE_CURRENT_SOURCE_DIR}/res/kity-symbolic.svg\" DESTINATION \"\${THEME_DIR}/status/symbolic\")
            file(COPY \"${CMAKE_CURRENT_SOURCE_DIR}/res/kity-listening-symbolic.svg\" DESTINATION \"\${THEME_DIR}/apps/symbolic\")
            file(COPY \"${CMAKE_CURRENT_SOURCE_DIR}/res/kity-listening-symbolic.svg\" DESTINATION \"\${THEME_DIR}/status/symbolic\")
            file(COPY \"${CMAKE_CURRENT_SOURCE_DIR}/res/kity.svg\" DESTINATION \"\${THEME_DIR}/apps/scalable\")
            file(COPY \"${CMAKE_CURRENT_SOURCE_DIR}/res/kity-listening.svg\" DESTINATION \"\${THEME_DIR}/apps/scalable\")
            file(COPY \"${CMAKE_CURRENT_SOURCE_DIR}/res/org.cesar.kity.svg\" DESTINATION \"\${THEME_DIR}/apps/scalable\")
            execute_process(COMMAND gtk-update-icon-cache -f -t \"\${THEME_DIR}\" ERROR_QUIET OUTPUT_QUIET)
        endif()
    endforeach()
")

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
    KF6::Notifications
    KF6::WindowSystem
)

install(TARGETS kity ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES org.cesar.kity.desktop
        DESTINATION ${KDE_INSTALL_APPDIR}
)

install(FILES kity_dolphin.desktop
        DESTINATION ${KDE_INSTALL_DATADIR}/kio/servicemenus
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

add_subdirectory(kcm)

