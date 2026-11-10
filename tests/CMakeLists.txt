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
