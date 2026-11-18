/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "contextmanager.h"
#include "knowledgemanager.h"
#include "system/weathermanager.h"
#include "system/websearchmanager.h"
#include "system/hardwaremanager.h"
#include "system/clipboardmanager.h"
#include "system/notificationmanager.h"
#include "system/windowmanager.h"
#include "system/configmanager.h"

#include <KLocalizedString>
#include <QRegularExpression>
#include <QSysInfo>

namespace Kity {

ContextManager &ContextManager::instance()
{
    static ContextManager s_instance;
    return s_instance;
}

ContextManager::ContextManager(QObject *parent)
    : QObject(parent)
{
}

QString ContextManager::fetchWeatherContext(const QString &query, QString *cardTypeOut, QVariantMap *cardDataOut) const
{
    static const QRegularExpression weatherRegex(
        QStringLiteral("(tiempo|clima|temperatura|llueve|llover|lluvia|fr[ií]o|calor|grados|pron[oó]stico|viento|soleado|nublado)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!query.contains(weatherRegex)) {
        return QString();
    }

    const auto wd = WeatherManager::instance().fetchWeatherData(query);
    if (!wd.isValid) {
        return QString();
    }

    if (cardTypeOut) {
        *cardTypeOut = QStringLiteral("weather");
    }
    if (cardDataOut) {
        *cardDataOut = wd.toMap();
    }

    return WeatherManager::instance().formatWeatherPromptContext(wd);
}

QString ContextManager::fetchWebSearchContext(const QString &query) const
{
    return WebSearchManager::instance().fetchWebSearchContext(query);
}

QString ContextManager::fetchDynamicLocalContext(const QString &query, const QString &intent) const
{
    Q_UNUSED(intent)
    QStringList ctx;
    static const QRegularExpression pkgRegex(
        QStringLiteral("(instalar|paquete|versi[oó]n de|cachyos|kernel|sistema|ordenador|pc|hardware|cpu|gpu)"),
        QRegularExpression::CaseInsensitiveOption);
    if (query.contains(pkgRegex)) {
        ctx << QStringLiteral("Linux Distribution: %1 (Arch-based rolling release, pacman package manager, Kernel %2, KDE Plasma 6.8 on Wayland).")
                   .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
        ctx << HardwareManager::instance().getSystemResourceSummary(QStringLiteral("all"));
    }
    return ctx.join(QStringLiteral("\n"));
}

QString ContextManager::fetchKnowledgeBaseContext(const QString &query) const
{
    const int maxSnippets = ConfigManager::instance().knowledgeBaseMaxSnippets();
    return KnowledgeManager::instance().fetchKnowledgeBaseContext(query, maxSnippets);
}

QString ContextManager::collectKWinWindowsInfo() const
{
    return WindowManager::instance().windowsSummary();
}

QString ContextManager::collectRecentNotificationsContext() const
{
    return NotificationManager::instance().collectRecentNotificationsContext();
}

ContextResult ContextManager::gatherContext(const QString &userQuery)
{
    ContextResult res;
    res.effectivePrompt = userQuery;

    // 1. Weather context
    const QString weatherCtx = fetchWeatherContext(userQuery, &res.pendingCardType, &res.pendingCardData);
    if (!weatherCtx.isEmpty()) {
        res.context.append(weatherCtx + QStringLiteral("\n\n"));
    }

    // 2. Proactive Web Search grounding (only for real-time, current events or explicit web queries)
    static const QRegularExpression webSignalRx(
        QStringLiteral("(202[4-9]|203\\d|noticias?|elecci[oó]n|elecciones|gobierno|precio|cotizaci[oó]n|estreno|actualidad|pr[oó]xim[ao]s?|hoy|ayer|mañana|suceso|falleci[oó]|resultado|partido|campe[oó]n|busca\\s+en|buscar\\s+en|b[uú]squeda\\s+web)"),
        QRegularExpression::CaseInsensitiveOption);
    if (userQuery.contains(webSignalRx)) {
        const QString webCtx = fetchWebSearchContext(userQuery);
        if (!webCtx.isEmpty()) {
            res.context.append(webCtx + QStringLiteral("\n\n"));
        }
    }

    // 3. Dynamic local OS & hardware context
    const QString localCtx = fetchDynamicLocalContext(userQuery, QStringLiteral("auto"));
    if (!localCtx.isEmpty()) {
        res.context.append(localCtx + QStringLiteral("\n\n"));
    }

    // 4. Local RAG Knowledge base context
    if (ConfigManager::instance().knowledgeBaseEnabled()) {
        const QString kbCtx = fetchKnowledgeBaseContext(userQuery);
        if (!kbCtx.isEmpty()) {
            res.context.append(kbCtx + QStringLiteral("\n\n"));
        }
    }

    // 5. Wayland primary selection / clipboard inspection
    static const QRegularExpression clipRegex(
        QStringLiteral("(portapapeles|clipboard|copiado|he copiado|acabo de copiar|texto copiado|selecci[oó]n|seleccionado|he seleccionado|lo que seleccion[eé])"),
        QRegularExpression::CaseInsensitiveOption);
    if (userQuery.contains(clipRegex) && !userQuery.contains(QStringLiteral("contenido del portapapeles")) && !userQuery.contains(QStringLiteral("en el portapapeles"))) {
        const QString clipText = ClipboardManager::instance().readWaylandSelectionOrClipboard();
        if (!clipText.isEmpty()) {
            res.effectivePrompt = QStringLiteral(
                "Contenido capturado de la selección activa o portapapeles del sistema:\n\"\"\"\n%1\n\"\"\"\n\n"
                "Con base en ese texto seleccionado o copiado, responde en español de España a mi petición: %2")
                .arg(clipText.left(6000), userQuery);
        } else {
            res.handledEarly = true;
            res.earlyAssistantResponse = i18n("No hay texto seleccionado ni copiado en el portapapeles en este momento. Selecciona o copia un fragmento de texto primero y vuelve a intentarlo.");
            return res;
        }
    }

    // 6. Notification Summaries & Priority Alerts (Apple Intelligence style)
    static const QRegularExpression notifSignal(
        QStringLiteral("(?:notificaci[oó]n|notificaciones|avisos?\\s+del?\\s+sistema|alertas?|mensajes?\\s+pendientes?)"),
        QRegularExpression::CaseInsensitiveOption);
    if (userQuery.contains(notifSignal) && (userQuery.contains(QLatin1String("resume")) || userQuery.contains(QLatin1String("tengo")) || userQuery.contains(QLatin1String("hay")) || userQuery.contains(QLatin1String("cu[aá]l")) || userQuery.contains(QLatin1String("qu[eé]")))) {
        const QString notifContext = collectRecentNotificationsContext();
        res.effectivePrompt = QStringLiteral(
            "Eres el motor de Resumen Inteligente y Priorización de Notificaciones de Kity (estilo Apple Intelligence macOS Sequoia/Tahoe).\n"
            "A continuación tienes el registro de avisos y eventos del sistema recientes:\n\n"
            "%1\n\n"
            "Instrucciones:\n"
            "- Sintetiza y resume de forma ejecutiva las notificaciones y avisos recibidos.\n"
            "- Agrupa por aplicación o servicio si hay varias.\n"
            "- Destaca claramente si hay alguna alerta crítica o prioritaria (por ejemplo, fallos de servicio, batería baja, errores).\n"
            "- Si no hay notificaciones críticas pendientes, dilo de forma clara y tranquilizadora.\n"
            "- Responde siempre en español de España de forma concisa y natural.")
            .arg(notifContext);
    }

    return res;
}

} // namespace Kity
