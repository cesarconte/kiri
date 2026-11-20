/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import CachyOS.UI

import "cards" as Cards

Item {
    id: spotlightRoot

    anchors.fill: parent

    required property var windowRoot
    property alias spotlightInputText: spotlightSearchInput.text

    signal startMoveRequested()
    signal expandToFullRequested()
    signal openPhotoDialogRequested()
    signal openFileDialogRequested()
    signal openWritingToolsRequested()
    signal openVisualIntelligenceRequested()
    signal openMathNotesRequested()

    DragHandler {
        target: null
        onActiveChanged: if (active) spotlightRoot.startMoveRequested()
    }

    // -----------------------------------------------------------------
    // STATE 1A: "Looking into it" Working Pill (05_siri_working_pill.png)
    // -----------------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 20
        spacing: 14
        visible: spotlightRoot.windowRoot.compactWorking

        // Ring of 8 glowing white dots of varying sizes
        Item {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32

            Repeater {
                model: 8

                Rectangle {
                    required property int index
                    readonly property real angle: (index * Math.PI / 4.0) + (spotlightRoot.windowRoot.animTime * 4.2)
                    readonly property real dotSize: 2.5 + ((index % 4) * 1.4)
                    width: dotSize
                    height: dotSize
                    radius: dotSize / 2
                    color: "#FFFFFF"
                    opacity: 0.45 + 0.55 * Math.sin(angle)
                    x: 16 + 10.5 * Math.cos(index * Math.PI / 4.0) - width / 2
                    y: 16 + 10.5 * Math.sin(index * Math.PI / 4.0) - height / 2
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: {
                if (kityClient.state === "transcribing") {
                    return i18n("Transcribiendo audio…");
                }
                if (kityClient.statusMessage.length > 0) {
                    return kityClient.statusMessage;
                }
                return i18n("Looking into it");
            }
            color: "#FFFFFF"
            font.pixelSize: 17
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        TapHandler {
            onTapped: {
                if (kityClient.state === "thinking") {
                    kityClient.stopGeneration();
                }
            }
        }
    }

    // -----------------------------------------------------------------
    // STATE 1B: "Search or Ask" + Quick Categories (08_spotlight_search_or_ask.png)
    // -----------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: spotlightRoot.windowRoot.compactCategoriesVisible ? 12 : 5
        spacing: spotlightRoot.windowRoot.compactCategoriesVisible ? 10 : 0
        visible: !spotlightRoot.windowRoot.compactWorking && !spotlightRoot.windowRoot.compactAnswerVisible

        // Top Pill: "Search or Ask" + Live Math + Mic
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            radius: 24
            color: spotlightRoot.windowRoot.compactCategoriesVisible ? Qt.rgba(0.06, 0.07, 0.10, 0.92) : "transparent"
            border.width: spotlightRoot.windowRoot.compactCategoriesVisible ? 1 : 0
            border.color: Qt.rgba(1, 1, 1, 0.20)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 14
                spacing: 10

                QQC2.TextField {
                    id: spotlightSearchInput
                    Layout.fillWidth: true
                    focus: spotlightRoot.windowRoot.compactMode && !spotlightRoot.windowRoot.compactAnswerVisible && !spotlightRoot.windowRoot.compactWorking
                    placeholderText: i18n("Search or Ask")
                    placeholderTextColor: Qt.rgba(1, 1, 1, 0.48)
                    color: "#FFFFFF"
                    font.pixelSize: 16
                    background: null

                    Keys.onEscapePressed: {
                        if (spotlightRoot.windowRoot.compactAnswerVisible) {
                            spotlightRoot.windowRoot.compactAnswerVisible = false;
                            spotlightRoot.windowRoot.compactCategoriesVisible = false;
                        } else if (spotlightRoot.windowRoot.compactCategoriesVisible) {
                            spotlightRoot.windowRoot.compactCategoriesVisible = false;
                            spotlightRoot.windowRoot.selectedCategoryIndex = -1;
                        } else {
                            spotlightRoot.windowRoot.hide();
                        }
                    }

                    Keys.onDownPressed: {
                        if (!spotlightRoot.windowRoot.compactCategoriesVisible) {
                            spotlightRoot.windowRoot.compactCategoriesVisible = true;
                            spotlightRoot.windowRoot.selectedCategoryIndex = 0;
                        } else {
                            spotlightRoot.windowRoot.selectedCategoryIndex = (spotlightRoot.windowRoot.selectedCategoryIndex + 1) % spotlightRoot.windowRoot.spotlightCategories.length;
                        }
                    }
                    Keys.onUpPressed: {
                        if (spotlightRoot.windowRoot.selectedCategoryIndex > 0) {
                            spotlightRoot.windowRoot.selectedCategoryIndex--;
                        } else {
                            spotlightRoot.windowRoot.selectedCategoryIndex = -1;
                            spotlightRoot.windowRoot.compactCategoriesVisible = false;
                        }
                    }
                    Keys.onTabPressed: {
                        if (!spotlightRoot.windowRoot.compactCategoriesVisible) {
                            spotlightRoot.windowRoot.compactCategoriesVisible = true;
                            spotlightRoot.windowRoot.selectedCategoryIndex = 0;
                        } else {
                            spotlightRoot.windowRoot.selectedCategoryIndex = (spotlightRoot.windowRoot.selectedCategoryIndex + 1) % spotlightRoot.windowRoot.spotlightCategories.length;
                        }
                    }
                    Keys.onBacktabPressed: {
                        if (spotlightRoot.windowRoot.selectedCategoryIndex > 0) {
                            spotlightRoot.windowRoot.selectedCategoryIndex--;
                        } else {
                            spotlightRoot.windowRoot.selectedCategoryIndex = -1;
                            spotlightRoot.windowRoot.compactCategoriesVisible = false;
                        }
                    }
                    Keys.onReturnPressed: event => handleAccept(event)
                    Keys.onEnterPressed: event => handleAccept(event)

                    function handleAccept(event) {
                        if (spotlightRoot.windowRoot.selectedCategoryIndex >= 0 && spotlightRoot.windowRoot.selectedCategoryIndex < spotlightRoot.windowRoot.spotlightCategories.length) {
                            let cat = spotlightRoot.windowRoot.spotlightCategories[spotlightRoot.windowRoot.selectedCategoryIndex].category;
                            if (cat === "actions" || cat === "clipboard") {
                                spotlightRoot.windowRoot.compactAnswerVisible = true;
                            }
                            kityClient.launchSpotlightCategory(cat);
                            spotlightRoot.windowRoot.selectedCategoryIndex = -1;
                            event.accepted = true;
                            return;
                        }
                        if (spotlightRoot.windowRoot.liveMathPreview.length > 0) {
                            kityClient.copyToClipboard(spotlightRoot.windowRoot.liveMathPreview);
                        }
                        if (text.trim().length > 0) {
                            spotlightRoot.windowRoot.compactAnswerVisible = true;
                            kityClient.sendMessage(text.trim(), false);
                            text = "";
                            event.accepted = true;
                        }
                    }

                    onTextChanged: {
                        spotlightRoot.windowRoot.restartMathPreviewTimer();
                        if (text.length > 0) {
                            spotlightRoot.windowRoot.selectedCategoryIndex = -1;
                        }
                    }
                }

                // Live Math Preview Pill (macOS Spotlight Instant Calc)
                Rectangle {
                    visible: spotlightRoot.windowRoot.liveMathPreview.length > 0
                    Layout.preferredHeight: 26
                    radius: 13
                    color: Qt.rgba(0.0, 0.88, 0.55, 0.22)
                    border.width: 1
                    border.color: Qt.rgba(0.0, 0.88, 0.55, 0.50)
                    Layout.preferredWidth: mathLabel.implicitWidth + 16

                    HoverHandler { id: mathPillHover; cursorShape: Qt.PointingHandCursor }

                    Text {
                        id: mathLabel
                        anchors.centerIn: parent
                        text: "= " + spotlightRoot.windowRoot.liveMathPreview
                        color: "#FFFFFF"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    TapHandler {
                        onTapped: {
                            kityClient.copyToClipboard(spotlightRoot.windowRoot.liveMathPreview);
                        }
                    }
                }

                // Microphone icon
                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: 15
                    color: kityClient.isRecording ? "#FF375F" : "transparent"

                    CachyIcon {
                        anchors.centerIn: parent
                        size: 16
                        name: "audio-input-microphone-symbolic"
                        color: Qt.rgba(1, 1, 0.82, 0.82)
                    }

                    TapHandler {
                        onTapped: kityClient.toggleRecording()
                    }
                }

                // Expand to Full Kity App (⤢) button
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: expandPillHover.hovered ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(1, 1, 1, 0.12)

                    HoverHandler { id: expandPillHover; cursorShape: Qt.PointingHandCursor }

                    Text {
                        anchors.centerIn: parent
                        text: "⤢"
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        font.bold: true
                    }

                    TapHandler {
                        onTapped: {
                            spotlightRoot.windowRoot.compactCategoriesVisible = false;
                            spotlightRoot.expandToFullRequested();
                        }
                    }
                }
            }
        }

        // Bottom Card: Applications ⌘1, Files ⌘2, Actions ⌘3, Clipboard ⌘4
        Rectangle {
            visible: spotlightRoot.windowRoot.compactCategoriesVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 18
            color: Qt.rgba(0.10, 0.11, 0.15, 0.90)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.16)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2

                Repeater {
                    model: spotlightRoot.windowRoot.spotlightCategories

                    Rectangle {
                        id: spotRow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 10
                        color: (spotRow.index === spotlightRoot.windowRoot.selectedCategoryIndex || spotRowHover.hovered)
                            ? Qt.rgba(1, 1, 1, 0.14)
                            : "transparent"
                        border.width: spotRow.index === spotlightRoot.windowRoot.selectedCategoryIndex ? 1 : 0
                        border.color: Qt.rgba(1, 1, 1, 0.25)

                        HoverHandler {
                            id: spotRowHover
                            cursorShape: Qt.PointingHandCursor
                            onHoveredChanged: {
                                if (hovered) {
                                    spotlightRoot.windowRoot.selectedCategoryIndex = spotRow.index;
                                }
                            }
                        }
                        TapHandler {
                            onTapped: {
                                if (spotRow.modelData.category === "actions" || spotRow.modelData.category === "clipboard") {
                                    spotlightRoot.windowRoot.compactAnswerVisible = true;
                                }
                                kityClient.launchSpotlightCategory(spotRow.modelData.category);
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12

                            CachyIcon {
                                name: spotRow.modelData.icon
                                size: 16
                                color: "#FFFFFF"
                            }

                            Text {
                                Layout.fillWidth: true
                                text: spotRow.modelData.label
                                color: "#FFFFFF"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                            }

                            Text {
                                text: spotRow.modelData.shortcut
                                color: Qt.rgba(1, 1, 1, 0.42)
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------
    // STATE 1C: Spotlight Answer Card (06_siri_spotlight_window.png)
    // -----------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        visible: !spotlightRoot.windowRoot.compactWorking && spotlightRoot.windowRoot.compactAnswerVisible

        // Top Header: Circular ✕ (left), Prompt Pill (center-right), Circular Expand ⤢ (right)
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 14
                color: Qt.rgba(1, 1, 1, 0.14)

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: "#FFFFFF"
                    font.pixelSize: 12
                    font.bold: true
                }

                TapHandler {
                    onTapped: {
                        spotlightRoot.windowRoot.compactAnswerVisible = false;
                        spotlightRoot.windowRoot.hide();
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Prompt Pill
            Rectangle {
                Layout.maximumWidth: 240
                Layout.preferredHeight: 26
                Layout.preferredWidth: Math.min(240, spotPromptLabel.implicitWidth + 20)
                radius: 13
                color: Qt.rgba(1, 1, 1, 0.08)

                Text {
                    id: spotPromptLabel
                    anchors.centerIn: parent
                    width: parent.width - 16
                    elide: Text.ElideRight
                    color: Qt.rgba(1, 1, 1, 0.72)
                    font.pixelSize: 12
                    text: {
                        for (let i = kityClient.messages.length - 1; i >= 0; --i) {
                            if (kityClient.messages[i].role === "user") {
                                return kityClient.messages[i].content;
                            }
                        }
                        return i18n("Tell me what's new with Kity...");
                    }
                }
            }

            // Expand ⤢ Button (Transitions to Full Siri App Window!)
            Rectangle {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 14
                color: Qt.rgba(1, 1, 1, 0.14)

                Text {
                    anchors.centerIn: parent
                    text: "⤢"
                    color: "#FFFFFF"
                    font.pixelSize: 13
                    font.bold: true
                }

                TapHandler {
                    onTapped: spotlightRoot.expandToFullRequested()
                }
            }
        }

        // Scrollable Answer Content (Strictly vertical, zero horizontal overflow)
        Flickable {
            id: spotAnswerFlickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: spotAnswerCol.implicitHeight + 8
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                policy: QQC2.ScrollBar.AsNeeded
            }

            ColumnLayout {
                id: spotAnswerCol
                width: Math.max(100, spotAnswerFlickable.width - 16)
                spacing: 10

                TextEdit {
                    id: spotAnswerText
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    cursorVisible: false
                    activeFocusOnPress: false
                    wrapMode: TextEdit.Wrap
                    color: "#FFFFFF"
                    font.pixelSize: 13
                    textFormat: TextEdit.RichText
                    onLinkActivated: link => Qt.openUrlExternally(link)
                    text: {
                        if (kityClient.streamingText.length > 0) {
                            return kityClient.renderMarkdown(kityClient.streamingText);
                        }
                        for (let i = kityClient.messages.length - 1; i >= 0; --i) {
                            if (kityClient.messages[i].role === "assistant") {
                                return kityClient.renderMarkdown(kityClient.messages[i].content);
                            }
                        }
                        return kityClient.renderMarkdown("### Kity AI\nAsk anything or click **⤢** to open the full Kity conversation app.");
                    }
                }

                // Generative Adaptive Cards inside Compact Spotlight Mode
                Cards.WeatherCard {
                    visible: spotlightRoot.windowRoot.lastAssistantMessage && spotlightRoot.windowRoot.lastAssistantMessage.cardType === "weather" && spotlightRoot.windowRoot.lastAssistantMessage.cardData !== undefined
                    Layout.fillWidth: true
                    cardData: (spotlightRoot.windowRoot.lastAssistantMessage && spotlightRoot.windowRoot.lastAssistantMessage.cardData) || ({})
                }

                Cards.MediaCard {
                    visible: spotlightRoot.windowRoot.lastAssistantMessage && spotlightRoot.windowRoot.lastAssistantMessage.cardType === "media" && spotlightRoot.windowRoot.lastAssistantMessage.cardData !== undefined
                    Layout.fillWidth: true
                    cardData: (spotlightRoot.windowRoot.lastAssistantMessage && spotlightRoot.windowRoot.lastAssistantMessage.cardData) || ({})
                    onMediaActionTriggered: action => kityClient.controlMedia(action)
                }

                Cards.EventCard {
                    visible: spotlightRoot.windowRoot.lastAssistantMessage && spotlightRoot.windowRoot.lastAssistantMessage.cardType === "event" && spotlightRoot.windowRoot.lastAssistantMessage.cardData !== undefined
                    Layout.fillWidth: true
                    cardData: (spotlightRoot.windowRoot.lastAssistantMessage && spotlightRoot.windowRoot.lastAssistantMessage.cardData) || ({})
                    onOpenCalendarRequested: kityClient.openKOrganizer()
                }

                Cards.EmailCard {
                    visible: spotlightRoot.windowRoot.lastAssistantMessage && spotlightRoot.windowRoot.lastAssistantMessage.cardType === "email" && spotlightRoot.windowRoot.lastAssistantMessage.cardData !== undefined
                    Layout.fillWidth: true
                    cardData: (spotlightRoot.windowRoot.lastAssistantMessage && spotlightRoot.windowRoot.lastAssistantMessage.cardData) || ({})
                    onSendRequested: kityClient.sendPendingEmail()
                    onOpenProtonRequested: kityClient.openPendingEmailInProton()
                    onDiscardRequested: kityClient.discardPendingEmail()
                }
            }
        }

        // Quick Action Chips: [Copiar] [Escuchar]
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: !spotlightRoot.windowRoot.compactWorking && spotlightRoot.windowRoot.compactAnswerVisible

            Rectangle {
                Layout.preferredHeight: 24
                radius: 12
                color: spotAnswerCopyHover.hovered ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, 0.10)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.14)
                Layout.preferredWidth: spotAnswerCopyRow.implicitWidth + 16

                HoverHandler { id: spotAnswerCopyHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        for (let i = kityClient.messages.length - 1; i >= 0; --i) {
                            if (kityClient.messages[i].role === "assistant") {
                                kityClient.copyToClipboard(kityClient.messages[i].content);
                                break;
                            }
                        }
                    }
                }

                RowLayout {
                    id: spotAnswerCopyRow
                    anchors.centerIn: parent
                    spacing: 6
                    CachyIcon {
                        name: "edit-copy-symbolic"
                        size: 12
                        color: "#FFFFFF"
                    }
                    Text {
                        text: i18n("Copiar")
                        color: "#FFFFFF"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                }
            }

            Rectangle {
                Layout.preferredHeight: 24
                radius: 12
                color: spotAnswerSpeakHover.hovered ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, 0.10)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.14)
                Layout.preferredWidth: spotAnswerSpeakRow.implicitWidth + 16

                HoverHandler { id: spotAnswerSpeakHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        if (kityClient.isSpeaking) {
                            kityClient.stopSpeaking();
                        } else {
                            for (let i = kityClient.messages.length - 1; i >= 0; --i) {
                                if (kityClient.messages[i].role === "assistant") {
                                    kityClient.speakText(kityClient.messages[i].content);
                                    break;
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    id: spotAnswerSpeakRow
                    anchors.centerIn: parent
                    spacing: 6
                    CachyIcon {
                        name: kityClient.isSpeaking ? "media-playback-stop-symbolic" : "audio-volume-high-symbolic"
                        size: 12
                        color: kityClient.isSpeaking ? "#FF375F" : "#FFFFFF"
                    }
                    Text {
                        text: kityClient.isSpeaking ? i18n("Detener voz") : i18n("Escuchar")
                        color: "#FFFFFF"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        // Bottom Bar: Circular + (left), "Ask Siri/Kity" Pill (center), Circular Mic (right)
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 15
                color: compactPlusHover.hovered ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(1, 1, 1, 0.14)

                HoverHandler { id: compactPlusHover; cursorShape: Qt.PointingHandCursor }

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: "#FFFFFF"
                    font.pixelSize: 16
                }

                TapHandler {
                    onTapped: compactPlusMenu.popup()
                }

                QQC2.Menu {
                    id: compactPlusMenu

                    QQC2.MenuItem {
                        icon.name: "image-x-generic"
                        text: i18n("Fotos…")
                        onTriggered: spotlightRoot.openPhotoDialogRequested()
                    }

                    QQC2.MenuItem {
                        icon.name: "document-open"
                        text: i18n("Archivos…")
                        onTriggered: spotlightRoot.openFileDialogRequested()
                    }

                    QQC2.MenuSeparator {}

                    QQC2.MenuItem {
                        icon.name: "document-edit-symbolic"
                        text: i18n("Herramientas de Escritura")
                        onTriggered: {
                            spotlightRoot.expandToFullRequested();
                            spotlightRoot.openWritingToolsRequested();
                        }
                    }

                    QQC2.MenuItem {
                        icon.name: "camera-photo-symbolic"
                        text: i18n("Inteligencia Visual (Capturar pantalla)")
                        onTriggered: {
                            spotlightRoot.expandToFullRequested();
                            spotlightRoot.openVisualIntelligenceRequested();
                        }
                    }

                    QQC2.MenuItem {
                        icon.name: "accessories-calculator"
                        text: i18n("Notas Matemáticas")
                        onTriggered: {
                            spotlightRoot.expandToFullRequested();
                            spotlightRoot.openMathNotesRequested();
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                radius: 16
                color: Qt.rgba(1, 1, 1, 0.12)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.18)

                QQC2.TextField {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    placeholderText: i18n("Ask Kity")
                    placeholderTextColor: Qt.rgba(1, 1, 1, 0.55)
                    color: "#FFFFFF"
                    font.pixelSize: 13
                    background: null
                    onAccepted: {
                        if (text.trim().length > 0) {
                            kityClient.sendMessage(text.trim(), false);
                            text = "";
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 15
                color: kityClient.isRecording ? "#FF375F" : Qt.rgba(1, 1, 1, 0.14)

                CachyIcon {
                    anchors.centerIn: parent
                    size: 15
                    name: "audio-input-microphone-symbolic"
                    color: "#FFFFFF"
                }

                TapHandler {
                    onTapped: kityClient.toggleRecording()
                }
            }
        }
    }
}
