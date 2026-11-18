/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "visualintelligencecontroller.h"
#include "visionengine.h"
#include "contextmanager.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>

#include <KLocalizedString>
#include <KNotification>

namespace Kity {

VisualIntelligenceController &VisualIntelligenceController::instance()
{
    static VisualIntelligenceController s_instance;
    return s_instance;
}

VisualIntelligenceController::VisualIntelligenceController(QObject *parent)
    : QObject(parent)
{
}

VisualResult VisualIntelligenceController::process(const QString &captureMode, const QString &action, const QString &customQuestion)
{
    VisualResult result;
    const QString mode = captureMode.isEmpty() ? QStringLiteral("region") : captureMode;
    const bool isDirectFile = mode.startsWith(QStringLiteral("file:"));
    QString savedPath;

    if (isDirectFile) {
        savedPath = mode.mid(5);
    } else {
        const QString tmpShot = QDir::tempPath() + QStringLiteral("/kity_livetext_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
        savedPath = VisionEngine::instance().captureScreenToPng(tmpShot, mode);
        if (savedPath.isEmpty()) {
            result.errorMessage = i18n("Captura de pantalla cancelada o no disponible.");
            return result;
        }
    }

    if (!QFileInfo::exists(savedPath)) {
        result.errorMessage = i18n("El archivo de imagen no existe: %1", savedPath);
        return result;
    }

    const QString ocrText = VisionEngine::instance().extractScreenOcrText(savedPath);
    const QString kwinInfo = isDirectFile ? QString() : ContextManager::instance().collectKWinWindowsInfo();
    const QString base64Image = VisionEngine::instance().encodeImageToBase64(savedPath);
    if (!isDirectFile) {
        QFile::remove(savedPath);
    }

    const QString modeLabel = isDirectFile ? QFileInfo(savedPath).fileName()
                      : (mode == QStringLiteral("region")) ? i18n("Región seleccionada")
                      : (mode == QStringLiteral("activewindow")) ? i18n("Ventana activa")
                      : i18n("Pantalla completa");
    result.modeLabel = modeLabel;

    if (action == QStringLiteral("ocr")) {
        result.handledEarly = true;
        result.success = true;

        if (ocrText.isEmpty()) {
            result.assistantFeedback = i18n("👁️ **Live Text (%1):** No se detectó texto legible en la zona capturada.", modeLabel);
            return result;
        }

        if (QGuiApplication::clipboard()) {
            QGuiApplication::clipboard()->setText(ocrText);
        }

        KNotification::event(QStringLiteral("notification"),
                             i18n("Live Text (OCR)"),
                             i18n("Texto copiado al portapapeles con éxito."),
                             QStringLiteral("org.cesar.kity"));

        const QVariantMap entities = VisionEngine::parseLiveTextEntities(ocrText);
        const QStringList urls = entities.value(QStringLiteral("urls")).toStringList();
        const QStringList emails = entities.value(QStringLiteral("emails")).toStringList();
        const QStringList phones = entities.value(QStringLiteral("phones")).toStringList();

        QString msg = i18n("👁️ **Live Text extraído (%1) — Copiado al portapapeles:**\n\n```\n%2\n```", modeLabel, ocrText);
        if (!urls.isEmpty() || !emails.isEmpty() || !phones.isEmpty()) {
            msg.append(i18n("\n\n**Datos rápidos detectados:**"));
            for (const QString &u : urls) msg.append(QStringLiteral("\n- 🔗 [%1](%1)").arg(u));
            for (const QString &e : emails) msg.append(QStringLiteral("\n- ✉️ [%1](mailto:%1)").arg(e));
            for (const QString &p : phones) msg.append(QStringLiteral("\n- 📞 `%1`").arg(p));
        }

        result.assistantFeedback = msg;
        return result;
    }

    if (ocrText.isEmpty() && kwinInfo.isEmpty() && base64Image.isEmpty()) {
        result.handledEarly = true;
        result.success = true;
        result.assistantFeedback = i18n("No se pudo extraer texto ni información de la captura (%1).", modeLabel);
        return result;
    }

    QString userLabel;
    QString aiInstruction;
    if (action == QStringLiteral("translate")) {
        userLabel = i18n("👁️ Traducir captura (%1)", modeLabel);
        aiInstruction = QStringLiteral("Traduce al español de España todo el texto visible extraído de la captura de pantalla.");
    } else if (action == QStringLiteral("explain")) {
        userLabel = i18n("👁️ Explicar / Analizar captura (%1)", modeLabel);
        if (!customQuestion.trimmed().isEmpty()) {
            aiInstruction = QStringLiteral("Responde en español de España a la siguiente pregunta sobre el contenido capturado: \"%1\"").arg(customQuestion.trimmed());
        } else {
            aiInstruction = QStringLiteral("Explica de forma clara y estructurada qué muestra este fragmento de pantalla.");
        }
    } else {
        userLabel = i18n("👁️ Inteligencia Visual (%1)", modeLabel);
        aiInstruction = QStringLiteral("Analiza y resume en español de España el siguiente contenido capturado en pantalla.");
    }

    result.userMessageLabel = userLabel;
    result.base64Image = base64Image;
    result.fullPrompt = QStringLiteral(
        "%1\n\n"
        "Contexto de ventanas KWin Wayland:\n%2\n\n"
        "Texto extraído mediante OCR (Live Text) de la captura (%3):\n\"\"\"\n%4\n\"\"\"")
        .arg(aiInstruction, kwinInfo, modeLabel, ocrText);
    result.success = true;
    return result;
}

} // namespace Kity
