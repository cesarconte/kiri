find_package(Qt6Test ${QT_MIN_VERSION} REQUIRED)

add_executable(test_wakeword test_wakeword.cpp)
target_link_libraries(test_wakeword PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME test_wakeword COMMAND test_wakeword)
