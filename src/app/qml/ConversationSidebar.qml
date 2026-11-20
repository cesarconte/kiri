/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import CachyOS.UI

ColumnLayout {
    id: sidebarRoot

    anchors.fill: parent
    anchors.leftMargin: 12
    anchors.rightMargin: 12
    anchors.bottomMargin: 12
    spacing: 8

    required property var windowRoot

    // Optional Sidebar Search Filter
    CachyTextField {
        visible: sidebarRoot.windowRoot.showSearchFilter
        Layout.fillWidth: true
        isSearch: true
        placeholderText: i18n("Filtrar conversaciones...")
        text: sidebarRoot.windowRoot.searchQuery
        onTextChanged: sidebarRoot.windowRoot.searchQuery = text
    }

    // Delete Confirmation Banner (macOS 27 Golden Gate: "Eliminar" -> "Eliminar")
    Rectangle {
        visible: sidebarRoot.windowRoot.deletingConversationId !== ""
        Layout.fillWidth: true
        implicitHeight: delBannerCol.implicitHeight + 16
        radius: 12
        color: Qt.rgba(0.9, 0.25, 0.25, 0.16)
        border.width: 1
        border.color: Qt.rgba(0.9, 0.25, 0.25, 0.45)

        ColumnLayout {
            id: delBannerCol
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                font.bold: true
                color: CachyTheme.textPrimary
                text: sidebarRoot.windowRoot.deletingConversationId === "__MULTI__"
                    ? i18n("¿Eliminar definitivamente las %1 conversaciones seleccionadas?", sidebarRoot.windowRoot.selectedConversationIds.length)
                    : i18n("¿Eliminar definitivamente esta conversación?")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                CachyButton {
                    Layout.fillWidth: true
                    text: i18n("Cancelar")
                    onClicked: sidebarRoot.windowRoot.deletingConversationId = ""
                }

                CachyButton {
                    Layout.fillWidth: true
                    destructive: true
                    text: i18n("Eliminar")
                    onClicked: {
                        if (sidebarRoot.windowRoot.deletingConversationId === "__MULTI__") {
                            kityClient.deleteConversations(sidebarRoot.windowRoot.selectedConversationIds);
                            sidebarRoot.windowRoot.selectedConversationIds = [];
                        } else {
                            kityClient.deleteConversation(sidebarRoot.windowRoot.deletingConversationId);
                        }
                        sidebarRoot.windowRoot.deletingConversationId = "";
                    }
                }
            }
        }
    }

    // Scrollable Conversation History Cards (2-column Grid or List)
    QQC2.ScrollView {
        id: sidebarScroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: availableWidth
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
        clip: true

        GridLayout {
            width: sidebarScroll.availableWidth
            columns: kityClient.sidebarViewMode === "grid" ? 2 : 1
            columnSpacing: 10
            rowSpacing: 10

            Repeater {
                model: kityClient.conversations

                Rectangle {
                    id: convCard
                    required property var modelData
                    required property int index

                    readonly property bool isCurrent: modelData.id === kityClient.activeConversationId
                    readonly property bool isMultiSelected: sidebarRoot.windowRoot.selectedConversationIds.indexOf(modelData.id) !== -1
                    readonly property bool isRenaming: sidebarRoot.windowRoot.renamingConversationId === modelData.id
                    readonly property bool matchesSearch: sidebarRoot.windowRoot.searchQuery.trim().length === 0
                        || (modelData.title && modelData.title.toLowerCase().indexOf(sidebarRoot.windowRoot.searchQuery.toLowerCase()) !== -1)
                        || (modelData.preview && modelData.preview.toLowerCase().indexOf(sidebarRoot.windowRoot.searchQuery.toLowerCase()) !== -1)

                    visible: matchesSearch
                    Layout.fillWidth: true
                    Layout.preferredWidth: kityClient.sidebarViewMode === "grid" ? (sidebarScroll.availableWidth - 10) / 2 : sidebarScroll.availableWidth
                    implicitHeight: convCardCol.implicitHeight + 22
                    radius: 16

                    color: (convCard.isCurrent || convCard.isMultiSelected)
                        ? (CachyTheme.isDark ? Qt.rgba(1, 1, 1, 0.13) : Qt.rgba(1, 1, 1, 0.88))
                        : (convHover.hovered ? CachyTheme.cardBackgroundHover : CachyTheme.cardBackground)

                    border.width: (convCard.isCurrent || convCard.isMultiSelected) ? 2 : 1
                    border.color: convCard.isMultiSelected
                        ? "#FF9F0A"
                        : (convCard.isCurrent ? "#007AFF" : CachyTheme.specularBorder)

                    Behavior on color { ColorAnimation { duration: 120 } }

                    HoverHandler {
                        id: convHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    // Left click or Ctrl+Left click (Multi-selection per Apple doc q7ny3g54v9lk)
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: (eventPoint, button) => {
                            if (eventPoint.modifiers & Qt.ControlModifier) {
                                let arr = sidebarRoot.windowRoot.selectedConversationIds.slice();
                                const pos = arr.indexOf(convCard.modelData.id);
                                if (pos === -1) {
                                    arr.push(convCard.modelData.id);
                                } else {
                                    arr.splice(pos, 1);
                                }
                                sidebarRoot.windowRoot.selectedConversationIds = arr;
                            } else {
                                sidebarRoot.windowRoot.selectedConversationIds = [];
                                kityClient.selectConversation(convCard.modelData.id);
                            }
                        }
                    }

                    // Right-click Context Menu (Fijar / Desfijar, Renombrar, Eliminar per Apple doc q7ny3g54v9lk)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: convContextMenu.popup()
                    }

                    QQC2.Menu {
                        id: convContextMenu

                        QQC2.MenuItem {
                            icon.name: convCard.modelData.pinned ? "window-unpin" : "window-pin"
                            text: convCard.modelData.pinned ? i18n("Desfijar") : i18n("Fijar")
                            onTriggered: kityClient.togglePinConversation(convCard.modelData.id)
                        }

                        QQC2.MenuItem {
                            icon.name: "edit-rename"
                            text: i18n("Renombrar…")
                            onTriggered: {
                                sidebarRoot.windowRoot.renamingConversationId = convCard.modelData.id;
                                sidebarRoot.windowRoot.renamingTitleText = convCard.modelData.title || "";
                            }
                        }

                        QQC2.MenuSeparator {}

                        QQC2.MenuItem {
                            icon.name: "edit-delete"
                            text: sidebarRoot.windowRoot.selectedConversationIds.length > 1
                                ? i18n("Eliminar %1 conversaciones…", sidebarRoot.windowRoot.selectedConversationIds.length)
                                : i18n("Eliminar…")
                            onTriggered: {
                                if (sidebarRoot.windowRoot.selectedConversationIds.length > 1) {
                                    sidebarRoot.windowRoot.deletingConversationId = "__MULTI__";
                                } else {
                                    sidebarRoot.windowRoot.deletingConversationId = convCard.modelData.id;
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        id: convCardCol
                        anchors.fill: parent
                        anchors.margins: 11
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: convCard.modelData.timeLabel || "Today"
                                font.pixelSize: 10
                                color: CachyTheme.textMuted
                                elide: Text.ElideRight
                            }

                            CachyIcon {
                                visible: convCard.modelData.pinned === true || convHover.hovered
                                name: "window-pin"
                                size: 13
                                color: CachyTheme.textSecondary
                                opacity: convCard.modelData.pinned ? 0.95 : 0.45

                                TapHandler {
                                    onTapped: kityClient.togglePinConversation(convCard.modelData.id)
                                }
                            }
                        }

                        // Inline Rename Editor when Renombrar is active
                        RowLayout {
                            visible: convCard.isRenaming
                            Layout.fillWidth: true
                            spacing: 4

                            QQC2.TextField {
                                id: renameField
                                Layout.fillWidth: true
                                text: sidebarRoot.windowRoot.renamingTitleText
                                font.pixelSize: 12
                                selectByMouse: true
                                onAccepted: {
                                    if (text.trim().length > 0) {
                                        kityClient.renameConversation(convCard.modelData.id, text.trim());
                                    }
                                    sidebarRoot.windowRoot.renamingConversationId = "";
                                }
                            }

                            QQC2.ToolButton {
                                text: "✓"
                                implicitWidth: 22
                                implicitHeight: 22
                                onClicked: {
                                    if (renameField.text.trim().length > 0) {
                                        kityClient.renameConversation(convCard.modelData.id, renameField.text.trim());
                                    }
                                    sidebarRoot.windowRoot.renamingConversationId = "";
                                }
                            }
                        }

                        QQC2.Label {
                            visible: !convCard.isRenaming
                            Layout.fillWidth: true
                            text: convCard.modelData.title || i18n("Kity AI")
                            font.pixelSize: 13
                            font.bold: true
                            color: CachyTheme.textPrimary
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: convCard.modelData.preview || ""
                            font.pixelSize: 11
                            color: CachyTheme.textSecondary
                            wrapMode: Text.WordWrap
                            maximumLineCount: kityClient.previewLines
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // Empty State in Sidebar (when 0 conversations exist)
    ColumnLayout {
        visible: kityClient.conversations.length === 0
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.topMargin: 24
        spacing: 10

        CachyIcon {
            name: "chat"
            Layout.alignment: Qt.AlignHCenter
            size: 36
            color: CachyTheme.textMuted
            opacity: 0.5
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: i18n("Sin conversaciones guardadas")
            font.pixelSize: 12
            font.weight: Font.Medium
            color: CachyTheme.textMuted
        }

        CachyButton {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("Nueva conversación")
            onClicked: kityClient.newConversation()
        }

        Item { Layout.fillHeight: true }
    }
}
