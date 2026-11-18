find_package(Qt6Test ${QT_MIN_VERSION} REQUIRED)
find_package(Qt6Qml ${QT_MIN_VERSION} REQUIRED)

add_executable(test_wakeword test_wakeword.cpp)
target_compile_definitions(test_wakeword PRIVATE KITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_wakeword PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Test
)

add_test(NAME test_wakeword COMMAND test_wakeword)

add_executable(test_mathnotes test_mathnotes.cpp ${CMAKE_SOURCE_DIR}/src/mathnotes.cpp)
target_include_directories(test_mathnotes PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_mathnotes PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_mathnotes COMMAND test_mathnotes)

add_executable(test_mobileserver
    test_mobileserver.cpp
    ${CMAKE_SOURCE_DIR}/src/mobile/mobileserver.cpp
    ${CMAKE_SOURCE_DIR}/src/mobile/mobile_resources.qrc
)
target_include_directories(test_mobileserver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/mobile
)
target_link_libraries(test_mobileserver PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    Qt6::DBus
    Qt6::Test
)
add_test(NAME test_mobileserver COMMAND test_mobileserver)

add_executable(test_kdictationclient
    test_kdictationclient.cpp
    ${CMAKE_SOURCE_DIR}/src/kdictationclient.cpp
)
target_include_directories(test_kdictationclient PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_kdictationclient PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Test
)
add_test(NAME test_kdictationclient COMMAND test_kdictationclient)

add_executable(test_appmanager
    test_appmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/system/windowmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/system/applicationmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/system/audiomanager.cpp
    ${CMAKE_SOURCE_DIR}/src/system/mediamanager.cpp
    ${CMAKE_SOURCE_DIR}/src/system/devicemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/system/timermanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/toolregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/launchapptool.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/closeapptool.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/mediacontroltool.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/timertool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/weathermanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/weathertool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/hardwaremanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/systemvitalstool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/clipboardmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/clipboardtool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/terminalmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/terminalcommandtool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/websearchmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/websearchtool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/calendarmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/calendareventtool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/emailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/emailtool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/notificationmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/notificationtool.cpp
    ${CMAKE_SOURCE_DIR}/src/system/kdeconnectmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/system/storagemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/system/configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/intentparser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/knowledgemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/visionengine.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/contextmanager.cpp
)
target_include_directories(test_appmanager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/system
    ${CMAKE_SOURCE_DIR}/src/tools
    ${CMAKE_SOURCE_DIR}/src/ai
)
target_link_libraries(test_appmanager PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Network
    Qt6::Test
    KF6::Service
    KF6::I18n
    KF6::Notifications
    KF6::ConfigCore
)
add_test(NAME test_appmanager COMMAND test_appmanager)
