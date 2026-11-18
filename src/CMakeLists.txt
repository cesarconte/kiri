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
        pipewirerecorder.cpp
        pipewirerecorder.h
        whisperengine.cpp
        whisperengine.h
        kdictationclient.cpp
        kdictationclient.h
        mathnotes.cpp
        mathnotes.h
        mobile/mobileserver.cpp
        mobile/mobileserver.h
        system/windowmanager.cpp
        system/windowmanager.h
        system/applicationmanager.cpp
        system/applicationmanager.h
        system/audiomanager.cpp
        system/audiomanager.h
        system/mediamanager.cpp
        system/mediamanager.h
        system/devicemanager.cpp
        system/devicemanager.h
        system/timermanager.cpp
        system/timermanager.h
        system/weathermanager.cpp
        system/weathermanager.h
        system/hardwaremanager.cpp
        system/hardwaremanager.h
        system/clipboardmanager.cpp
        system/clipboardmanager.h
        system/terminalmanager.cpp
        system/terminalmanager.h
        system/websearchmanager.cpp
        system/websearchmanager.h
        system/calendarmanager.cpp
        system/calendarmanager.h
        system/emailmanager.cpp
        system/emailmanager.h
        system/notificationmanager.cpp
        system/notificationmanager.h
        system/kdeconnectmanager.cpp
        system/kdeconnectmanager.h
        system/storagemanager.cpp
        system/storagemanager.h
        system/configmanager.cpp
        system/configmanager.h
        ai/contextmanager.cpp
        ai/contextmanager.h
        ai/intentparser.cpp
        ai/intentparser.h
        ai/knowledgemanager.cpp
        ai/knowledgemanager.h
        ai/visionengine.cpp
        ai/visionengine.h
        ai/llmengine.cpp
        ai/llmengine.h
        ai/writingtoolsmanager.cpp
        ai/writingtoolsmanager.h
        voice/voiceengine.cpp
        voice/voiceengine.h
        tools/itool.h
        tools/toolregistry.cpp
        tools/toolregistry.h
        tools/launchapptool.cpp
        tools/launchapptool.h
        tools/closeapptool.cpp
        tools/closeapptool.h
        tools/mediacontroltool.cpp
        tools/mediacontroltool.h
        tools/timertool.cpp
        tools/timertool.h
        tools/weathertool.cpp
        tools/weathertool.h
        tools/systemvitalstool.cpp
        tools/systemvitalstool.h
        tools/clipboardtool.cpp
        tools/clipboardtool.h
        tools/terminalcommandtool.cpp
        tools/terminalcommandtool.h
        tools/websearchtool.cpp
        tools/websearchtool.h
        tools/calendareventtool.cpp
        tools/calendareventtool.h
        tools/emailtool.cpp
        tools/emailtool.h
        tools/notificationtool.cpp
        tools/notificationtool.h
        ${kity_SRCS}
    QML_SOURCES
        qml/main.qml
        qml/SiriOrbAnimation.qml
        qml/cards/WeatherCard.qml
        qml/cards/MediaCard.qml
        qml/cards/EventCard.qml
        qml/cards/EmailCard.qml
    RESOURCES
        res/main.xml
        res/org.cesar.kity.svg
        res/kity.svg
        res/kity-symbolic.svg
        res/kity-listening.svg
        res/kity-listening-symbolic.svg
)

qt6_add_resources(org.cesar.kity "mobile_resources"
    FILES mobile/mobile_resources.qrc
)

target_include_directories(org.cesar.kity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/mobile
    ${CMAKE_CURRENT_SOURCE_DIR}/system
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${PIPEWIRE_INCLUDE_DIRS}
    ${WHISPER_INCLUDE_DIRS}
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
    ${PIPEWIRE_LIBRARIES}
    ${WHISPER_LIBRARIES}
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

# Install symbolic and app icons into Breeze and Breeze-Dark to prevent fallback to colored background
install(FILES res/org.cesar.kity.svg
              res/kity.svg
              res/kity-symbolic.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/breeze/apps/48
)
install(FILES res/kity-symbolic.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/breeze/status/22
)
install(FILES res/kity-symbolic.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/breeze/status/16
)

install(FILES res/org.cesar.kity.svg
              res/kity.svg
              res/kity-symbolic.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/breeze-dark/apps/48
)
install(FILES res/kity-symbolic.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/breeze-dark/status/22
)
install(FILES res/kity-symbolic.svg
              res/kity-listening-symbolic.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/breeze-dark/status/16
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
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::DBusAddons
    KF6::I18n
    KF6::Notifications
    KF6::WindowSystem
)

install(TARGETS kity ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES res/sounds/kity_on.wav
              res/sounds/kity_off.wav
        DESTINATION ${KDE_INSTALL_DATADIR}/kity/sounds
)

install(FILES org.cesar.kity.desktop
        DESTINATION ${KDE_INSTALL_APPDIR}
)

install(FILES kity_dolphin.desktop
        DESTINATION ${KDE_INSTALL_DATADIR}/kio/servicemenus
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

install(DIRECTORY mobile/web/
        DESTINATION ${KDE_INSTALL_DATADIR}/kity/mobile/web
)

add_subdirectory(kcm)

