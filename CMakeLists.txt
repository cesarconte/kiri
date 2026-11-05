cmake_minimum_required(VERSION 3.20)
project(kity VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QT_MIN_VERSION "6.7.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
set(KDE_INSTALL_QTPLUGINDIR "${KDE_INSTALL_LIBDIR}/qt6/plugins" CACHE PATH "Qt6 plugins directory" FORCE)
set(KDE_INSTALL_PLUGINDIR "${KDE_INSTALL_LIBDIR}/qt6/plugins" CACHE PATH "Plugins directory" FORCE)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)
include(ECMQtDeclareLoggingCategory)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    DBus
    Gui
    Network
    Quick
    Widgets
)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config
    CoreAddons
    DBusAddons
    I18n
    Notifications
    Runner
    Service
    WindowSystem
)

find_package(Plasma REQUIRED)
find_package(PlasmaQuick REQUIRED)

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
