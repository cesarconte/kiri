find_package(Qt6Test ${QT_MIN_VERSION} REQUIRED)
find_package(Qt6Qml ${QT_MIN_VERSION} REQUIRED)

add_executable(test_wakeword test_wakeword.cpp)
target_compile_definitions(test_wakeword PRIVATE KIRI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_wakeword PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Test
)

add_test(NAME test_wakeword COMMAND test_wakeword)
