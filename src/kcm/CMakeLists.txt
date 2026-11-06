# SPDX-FileCopyrightText: 2026 Kity contributors
# SPDX-License-Identifier: GPL-3.0-or-later

kcmutils_add_qml_kcm(kcm_kity
    SOURCES
        kcm_kity.cpp
        kcm_kity.h
)

target_link_libraries(kcm_kity PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Quick
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    KF6::KCMUtilsQuick
)
