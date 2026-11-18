/*
 * SPDX-FileCopyrightText: 2026 Kity contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import CachyOS.UI

CachyCard {
    id: root

    property var cardData: ({})

    signal sendRequested()
    signal openProtonRequested()
    signal discardRequested()

    Layout.fillWidth: true
    Layout.minimumWidth: 0

    contentItem: ColumnLayout {
        id: mainLayout
        spacing: CachyTheme.spacingMedium

        // 1. Header: Service icon, Recipient / Title and Status Badge
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: CachyTheme.spacingSmall

            CachyIcon {
                name: "mail-message-new"
                size: 20
                color: CachyTheme.accentColor
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2

                CachyHeading {
                    level: 3
                    text: root.cardData.toName || root.cardData.to || CachyI18n.i18n("Nuevo mensaje")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }

                Text {
                    text: root.cardData.to ? ("<" + root.cardData.to + ">") : ""
                    font.pixelSize: 11
                    color: CachyPalette.textSecondary
                    visible: text.length > 0 && root.cardData.toName !== root.cardData.to
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
            }

            // High-contrast status badge (WCAG-compliant Golden Gate amber/green/red)
            Rectangle {
                id: statusBadge
                readonly property string status: root.cardData.status || "draft"
                readonly property color tintColor: {
                    if (status === "sent") return CachyPalette.isDark ? "#30D158" : "#15803D";
                    if (status === "discarded") return CachyPalette.isDark ? "#FF453A" : "#B91C1C";
                    if (status === "awaiting_confirmation") return CachyPalette.isDark ? "#FFD60A" : "#92400E";
                    return CachyTheme.accentColor;
                }

                implicitWidth: Math.max(24, badgeRow.implicitWidth + 16)
                implicitHeight: 22
                radius: height / 2
                color: CachyTheme.withAlpha(status === "awaiting_confirmation" ? "#FF9500" : tintColor, CachyPalette.isDark ? 0.22 : 0.14)
                border.width: 1
                border.color: CachyTheme.withAlpha(tintColor, 0.45)

                RowLayout {
                    id: badgeRow
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 4

                    CachyIcon {
                        Layout.preferredWidth: 11
                        Layout.preferredHeight: 11
                        name: {
                            if (statusBadge.status === "sent") return "dialog-ok";
                            if (statusBadge.status === "discarded") return "dialog-cancel";
                            if (statusBadge.status === "awaiting_confirmation") return "dialog-warning";
                            return "document-edit";
                        }
                        size: 11
                        color: statusBadge.tintColor
                        visible: valid
                    }

                    Text {
                        text: {
                            if (statusBadge.status === "sent") return CachyI18n.i18n("Enviado");
                            if (statusBadge.status === "discarded") return CachyI18n.i18n("Descartado");
                            if (statusBadge.status === "awaiting_confirmation") return CachyI18n.i18n("Confirmación pendiente");
                            return CachyI18n.i18n("Borrador");
                        }
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        color: statusBadge.tintColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // 2. Subject Line
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: CachyTheme.spacingSmall

            Text {
                text: CachyI18n.i18n("Asunto:")
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: CachyPalette.textSecondary
            }

            Text {
                text: root.cardData.subject || CachyI18n.i18n("(Sin asunto)")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: CachyPalette.text
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
        }

        // 3. Body Preview Container (Glass pill)
        Rectangle {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            radius: CachyTheme.radiusSmall
            color: CachyPalette.surfacePill
            border.width: 1
            border.color: CachyPalette.specularBorder

            implicitHeight: bodyText.implicitHeight + (CachyTheme.spacingSmall * 2)

            Text {
                id: bodyText
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    margins: CachyTheme.spacingSmall
                }
                text: root.cardData.body || CachyI18n.i18n("(Sin contenido)")
                font.pixelSize: 12
                color: CachyPalette.text
                wrapMode: Text.WordWrap
            }
        }

        // 4. Action Buttons Footer (Adaptive Layout: Zero Overflow in Pill or Expanded modes)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: CachyTheme.spacingSmall
            visible: root.cardData.status !== "sent" && root.cardData.status !== "discarded"

            // Row for secondary actions (and primary button on wide surfaces)
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: CachyTheme.spacingSmall

                CachyButton {
                    destructive: true
                    text: CachyI18n.i18n("Descartar")
                    iconName: "edit-delete"
                    onClicked: root.discardRequested()
                }

                Item {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }

                CachyButton {
                    flat: true
                    text: CachyI18n.i18n("Ver en Proton")
                    iconName: "mail-message-new"
                    onClicked: root.openProtonRequested()
                }

                CachyButton {
                    primary: true
                    text: CachyI18n.i18n("Enviar correo")
                    iconName: "mail-sent"
                    visible: root.width >= 410
                    onClicked: root.sendRequested()
                }
            }

            // In compact pill mode (< 410px), Enviar button sits on its own row filling width cleanly
            CachyButton {
                primary: true
                text: CachyI18n.i18n("Enviar correo")
                iconName: "mail-sent"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: root.width < 410
                onClicked: root.sendRequested()
            }
        }
    }
}
