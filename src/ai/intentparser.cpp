/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "intentparser.h"
#include "../system/applicationmanager.h"
#include "../system/windowmanager.h"
#include "../system/audiomanager.h"
#include "../system/mediamanager.h"
#include "../system/devicemanager.h"
#include "../system/timermanager.h"
#include "../system/terminalmanager.h"
#include "../system/hardwaremanager.h"
#include "../system/clipboardmanager.h"
#include "../system/calendarmanager.h"
#include "../system/emailmanager.h"
#include "../system/notificationmanager.h"

#include <QDateTime>
#include <QProcess>
#include <QRegularExpression>
#include <KLocalizedString>

namespace Kity {

IntentParser &IntentParser::instance()
{
    static IntentParser s_instance;
    return s_instance;
}

IntentParser::IntentParser(QObject *parent)
    : QObject(parent)
{
}

IntentExecutionResult IntentParser::execute(const QString &input)
{
    IntentExecutionResult result;
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return result;
    }

    // 0. CachyOS System Update: "actualiza/actualizar el sistema / cachyos / equipo" o "cachy-update"
    static const QRegularExpression updateRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:actualiza|actualizar)(?:\\s+(?:el\\s+)?(?:sistema|equipo|ordenador|pc|cachyos|software))?[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(updateRegex) || trimmed.compare(QStringLiteral("cachy-update"), Qt::CaseInsensitive) == 0
        || trimmed.compare(QStringLiteral("actualizar"), Qt::CaseInsensitive) == 0
        || trimmed.compare(QStringLiteral("actualiza"), Qt::CaseInsensitive) == 0) {
        TerminalManager::instance().executeInKonsole(QStringLiteral("cachy-update"), QStringLiteral("Cachy-Update"), true);
        result.handled = true;
        result.feedback = i18n("Iniciando Cachy-Update para actualizar el sistema.");
        return result;
    }

    // 1. App Launch: "abre/inicia/lanza/ejecuta/arranca <app>"
    static const QRegularExpression launchRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:abre|inicia|lanza|ejecuta|arranca|abrir)\\s+(.+?)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch lMatch = launchRegex.match(trimmed);
    if (lMatch.hasMatch()) {
        const QString appQuery = lMatch.captured(1).trimmed();
        QString launchedName;
        if (ApplicationManager::instance().launchApp(appQuery, &launchedName)) {
            result.handled = true;
            result.feedback = i18n("Abriendo %1.", launchedName.isEmpty() ? appQuery : launchedName);
            return result;
        }
    }

    // 2. App Close: "cierra/termina/mata/cerrar <app>"
    static const QRegularExpression closeRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:cierra|termina|mata|cerrar)\\s+(.+?)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch cMatch = closeRegex.match(trimmed);
    if (cMatch.hasMatch()) {
        const QString appQuery = cMatch.captured(1).trimmed();
        QString closedName;
        if (WindowManager::instance().closeWindow(appQuery, &closedName) || ApplicationManager::instance().closeApp(appQuery, &closedName)) {
            result.handled = true;
            result.feedback = i18n("Cerrando %1.", closedName.isEmpty() ? appQuery : closedName);
            return result;
        }
    }

    // 2b. Window & Desktop Management (KWin Wayland)
    static const QRegularExpression minRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:minimiza|minimizar|oculta|ocultar)(?:\\s+la\\s+ventana)?(?:\\s+(.+?))?[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch minMatch = minRegex.match(trimmed);
    if (minMatch.hasMatch()) {
        const QString target = minMatch.captured(1).trimmed();
        if (WindowManager::instance().minimizeWindow(target)) {
            result.handled = true;
            result.feedback = target.isEmpty() ? i18n("Ventana minimizada.") : i18n("Minimizando %1.", target);
            return result;
        }
    }

    static const QRegularExpression maxRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:maximiza|maximizar|restaura|restaurar)(?:\\s+la\\s+ventana)?(?:\\s+(.+?))?[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch maxMatch = maxRegex.match(trimmed);
    if (maxMatch.hasMatch()) {
        const QString target = maxMatch.captured(1).trimmed();
        if (WindowManager::instance().maximizeWindow(target)) {
            result.handled = true;
            result.feedback = target.isEmpty() ? i18n("Ventana maximizada/restaurada.") : i18n("Maximizando %1.", target);
            return result;
        }
    }

    static const QRegularExpression desktopRegex(
        QStringLiteral("^(?:por\\s+favor\\s+)?(?:muestra|mostrar|ver)(?:\\s+el)?\\s+escritorio[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(desktopRegex)) {
        if (WindowManager::instance().showDesktop()) {
            result.handled = true;
            result.feedback = i18n("Mostrando el escritorio.");
            return result;
        }
    }

    // 2c. EasyEffects Audio Control
    static const QRegularExpression eeToggleRegex(
        QStringLiteral("(?:activa|activar|desactiva|desactivar|alterna|cambia)\\s+(?:los\\s+)?(?:efectos(?:\\s+de\\s+audio)?|easyeffects)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(eeToggleRegex)) {
        AudioManager::instance().toggleEasyEffectsBypass();
        result.handled = true;
        result.feedback = i18n("Alternando efectos de audio en EasyEffects.");
        return result;
    }

    static const QRegularExpression eePresetRegex(
        QStringLiteral("(?:pon|cambia\\s+al|activa\\s+el)?\\s*(?:perfil|preset)\\s+(?:de\\s+)?(?:audio|easyeffects)\\s+([a-zA-Z0-9_-]+)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch eeMatch = eePresetRegex.match(trimmed);
    if (eeMatch.hasMatch()) {
        const QString preset = eeMatch.captured(1).trimmed();
        AudioManager::instance().loadEasyEffectsPreset(preset);
        result.handled = true;
        result.feedback = i18n("Cargando perfil de audio '%1' en EasyEffects.", preset);
        return result;
    }

    // 3. Volume & Microphone Control via PipeWire / WirePlumber
    static const QRegularExpression volQueryRegex(
        QStringLiteral("(?:qu[eé]\\s+volumen|cu[aá]l\\s+es\\s+el\\s+volumen|dime\\s+el\\s+volumen|a\\s+qu[eé]\\s+volumen|nivel\\s+de\\s+(?:volumen|audio|sonido))"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volQueryRegex)) {
        result.handled = true;
        result.feedback = AudioManager::instance().getAudioVolumeStatus();
        return result;
    }

    static const QRegularExpression volExactRegex(
        QStringLiteral("(?:pon\\s+el\\s+|ajusta\\s+el\\s+|cambia\\s+el\\s+)?(?:volumen|sonido|audio)\\s+(?:a|al)\\s*(\\d{1,3})\\s*(?:%|por\\s+ciento)?"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch volExactMatch = volExactRegex.match(trimmed);
    if (volExactMatch.hasMatch()) {
        const int pct = qBound(0, volExactMatch.captured(1).toInt(), 100);
        AudioManager::instance().setVolume(pct);
        result.handled = true;
        result.feedback = i18n("Volumen ajustado al %1%.", pct);
        return result;
    }

    static const QRegularExpression volUpRegex(QStringLiteral("(?:sube|subir|aumenta|aumentar|sube el|subir el)\\s+(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volUpRegex)) {
        AudioManager::instance().stepVolume(5);
        result.handled = true;
        result.feedback = i18n("Subiendo el volumen.");
        return result;
    }

    static const QRegularExpression volDownRegex(QStringLiteral("(?:baja|bajar|reduce|reducir|baja el|bajar el)\\s+(?:volumen|sonido|audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(volDownRegex)) {
        AudioManager::instance().stepVolume(-5);
        result.handled = true;
        result.feedback = i18n("Bajando el volumen.");
        return result;
    }

    static const QRegularExpression muteRegex(QStringLiteral("(?:silencia|silenciar|mutea|mutear|quitar? el sonido|sin sonido|apaga el audio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(muteRegex)) {
        AudioManager::instance().setAudioMute(true);
        result.handled = true;
        result.feedback = i18n("Audio silenciado.");
        return result;
    }

    static const QRegularExpression unmuteRegex(QStringLiteral("(?:desilencia|desilenciar|reactiva el sonido|activa el sonido|quita el silencio)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(unmuteRegex)) {
        AudioManager::instance().setAudioMute(false);
        result.handled = true;
        result.feedback = i18n("Audio reactivado.");
        return result;
    }

    // 4. Media Playback Control
    static const QRegularExpression playPauseRegex(QStringLiteral("(?:pausa|pausar|reanuda|reanudar|play-pause|reproduce la m[uú]sica|pausa la m[uú]sica)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(playPauseRegex)) {
        MediaManager::instance().executeAction(QStringLiteral("play-pause"));
        result.handled = true;
        result.cardType = QStringLiteral("media");
        result.cardData = MediaManager::instance().queryActiveMedia().toMap();
        result.feedback = i18n("Alternando reproducción.");
        return result;
    }

    static const QRegularExpression nextTrackRegex(QStringLiteral("(?:siguiente|pasa(?:\\s+de)?\\s+(?:canci[oó]n|pista)|next)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(nextTrackRegex)) {
        MediaManager::instance().executeAction(QStringLiteral("next"));
        result.handled = true;
        result.cardType = QStringLiteral("media");
        result.cardData = MediaManager::instance().queryActiveMedia().toMap();
        result.feedback = i18n("Pasando a la siguiente pista.");
        return result;
    }

    static const QRegularExpression prevTrackRegex(QStringLiteral("(?:anterior|vuelve\\s+a\\s+la\\s+(?:canci[oó]n|pista)|previous)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(prevTrackRegex)) {
        MediaManager::instance().executeAction(QStringLiteral("previous"));
        result.handled = true;
        result.cardType = QStringLiteral("media");
        result.cardData = MediaManager::instance().queryActiveMedia().toMap();
        result.feedback = i18n("Volviendo a la pista anterior.");
        return result;
    }

    // 7. Power Profile
    static const QRegularExpression powerProfRegex(
        QStringLiteral("(?:pon|cambia\\s+a|activa\\s+(?:el\\s+)?(?:modo|perfil))\\s+(?:de\\s+)?(?:energ[ií]a\\s+)?(ahorro|rendimiento|equilibrado|balanceado)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch pMatch = powerProfRegex.match(trimmed);
    if (pMatch.hasMatch()) {
        const QString prof = pMatch.captured(1).toLower();
        QString profileId = QStringLiteral("balanced");
        if (prof.contains(QStringLiteral("ahorro"))) profileId = QStringLiteral("power-saver");
        else if (prof.contains(QStringLiteral("rendimiento"))) profileId = QStringLiteral("performance");

        DeviceManager::instance().setActivePowerProfile(profileId);
        result.handled = true;
        result.feedback = i18n("Perfil de energía establecido en %1.", prof);
        return result;
    }

    // 8. Do Not Disturb
    static const QRegularExpression dndRegex(QStringLiteral("(?:activa|activar|desactiva|desactivar)\\s+(?:el\\s+)?modo\\s+no\\s+molestar"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(dndRegex)) {
        const bool enable = trimmed.contains(QStringLiteral("activa"));
        DeviceManager::instance().setDoNotDisturb(enable);
        result.handled = true;
        result.feedback = enable ? i18n("Modo No Molestar activado.") : i18n("Modo No Molestar desactivado.");
        return result;
    }

    // 9. Battery Status
    static const QRegularExpression batteryRegex(QStringLiteral("(?:bater[ií]a|cu[aá]nta\\s+bater[ií]a|estado\\s+de\\s+(?:la\\s+)?bater[ií]a)"), QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(batteryRegex)) {
        result.handled = true;
        result.feedback = DeviceManager::instance().getLocalBatteryInfo();
        return result;
    }

    // 10. Timers
    static const QRegularExpression timerDirectRegex(
        QStringLiteral("(?:pon|programa|crea)?\\s*(?:un\\s+)?temporizador\\s+de\\s+(\\d+)\\s*(minutos?|segundos?|horas?)(?:\\s+para\\s+(.+))?"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch tMatch = timerDirectRegex.match(trimmed);
    if (tMatch.hasMatch()) {
        int val = tMatch.captured(1).toInt();
        const QString unit = tMatch.captured(2).toLower();
        const QString label = tMatch.captured(3).trimmed().isEmpty() ? QStringLiteral("Temporizador") : tMatch.captured(3).trimmed();

        int seconds = val;
        if (unit.startsWith(QStringLiteral("min"))) seconds *= 60;
        else if (unit.startsWith(QStringLiteral("hor"))) seconds *= 3600;

        TimerManager::instance().addTimer(seconds, label);
        result.handled = true;
        result.feedback = i18n("Temporizador de %1 establecido para %2.", tMatch.captured(1) + QLatin1Char(' ') + unit, label);
        return result;
    }

    // 14. Email Confirmation & Discard Actions (Safety Guardrails)
    if (EmailManager::instance().hasPendingDraft()) {
        static const QRegularExpression sendRegex(
            QStringLiteral("^(?:s[ií],?\\s*)?(?:env[ií]alo|confirmo(?:\\s+el\\s+env[ií]o)?|m[aá]ndalo|enviar(?:\\s+correo)?)$"),
            QRegularExpression::CaseInsensitiveOption);
        if (trimmed.contains(sendRegex)) {
            QString err;
            result.handled = true;
            if (EmailManager::instance().sendPendingDraft(&err)) {
                result.feedback = i18n("✓ Correo enviado con éxito mediante Proton Mail.");
            } else {
                result.feedback = err.isEmpty() ? i18n("No se pudo enviar el correo.") : err;
            }
            return result;
        }

        static const QRegularExpression discardRegex(
            QStringLiteral("^(?:no,?\\s*)?(?:b[oó]rralo|descarta(?:\\s+el\\s+correo|lo)?|elim[ií]nalo|cancela(?:\\s+el\\s+correo|lo)?)$"),
            QRegularExpression::CaseInsensitiveOption);
        if (trimmed.contains(discardRegex)) {
            result.handled = true;
            if (EmailManager::instance().discardPendingDraft()) {
                result.feedback = i18n("✕ Borrador de correo descartado y eliminado.");
            } else {
                result.feedback = i18n("No había ningún borrador de correo pendiente.");
            }
            return result;
        }
    }

    // 14b. Email Composition with Semantic Natural Language Extraction
    static const QRegularExpression emailRegex(
        QStringLiteral("(?:env[ií]a|redacta|escribe|manda)\\s+(?:un\\s+|una\\s+)?(?:correo|email|mensaje)"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(emailRegex)) {
        const EmailDraft parsed = EmailManager::instance().parsePrompt(trimmed);
        if (!parsed.to.isEmpty()) {
            const EmailDraft draft = EmailManager::instance().prepareDraft(parsed.to, parsed.subject, parsed.body, QStringLiteral("proton"));
            result.handled = true;
            result.cardType = QStringLiteral("email");
            result.cardData = draft.toCardMap();
            result.feedback = i18n("He preparado el borrador para %1. Puedes revisarlo en la tarjeta, enviarlo o descartarlo.", draft.toName.isEmpty() ? draft.to : draft.toName);
            return result;
        }
    }

    // 17. Screen Brightness Control
    static const QRegularExpression brightExactRegex(
        QStringLiteral("(?:pon|ajusta|cambia)\\s+(?:el\\s+)?brillo\\s+(?:a|al)\\s*(\\d{1,3})\\s*(?:%|por\\s+ciento)?"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch bMatch = brightExactRegex.match(trimmed);
    if (bMatch.hasMatch()) {
        const int pct = qBound(5, bMatch.captured(1).toInt(), 100);
        HardwareManager::instance().adjustScreenBrightness(0, pct);
        result.handled = true;
        result.feedback = i18n("Brillo ajustado al %1%.", pct);
        return result;
    }

    // 21. Time & Date Queries
    static const QRegularExpression timeRegex(
        QStringLiteral("^[¿¡]*(?:qu[eé]\\s+hora\\s+es|hora\\s+actual|dime\\s+la\\s+hora)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(timeRegex)) {
        const QTime now = QTime::currentTime();
        result.handled = true;
        result.feedback = i18n("Son las %1.", now.toString(QStringLiteral("HH:mm")));
        return result;
    }

    static const QRegularExpression dateRegex(
        QStringLiteral("^[¿¡]*(?:qu[eé]\\s+d[ií]a\\s+es(?:\\s+hoy)?|fecha(?:\\s+de\\s+hoy)?|dime\\s+la\\s+fecha(?:\\s+de\\s+hoy)?|qu[eé]\\s+fecha\\s+es(?:\\s+hoy)?)[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(dateRegex)) {
        const QDate today = QDate::currentDate();
        result.handled = true;
        result.feedback = i18n("Hoy es %1.", today.toString(QStringLiteral("dddd, d 'de' MMMM 'de' yyyy")));
        return result;
    }

    // 24. Instant Clipboard Query
    static const QRegularExpression clipQueryRegex(
        QStringLiteral(
            "^[¿¡]*(?:(?:puedes\\s+)?(?:dime|decirme|muestra|mu[eé]strame|mostrar(?:me)?|ens[eé][ñn]a(?:me)?|ense[ñn]ar(?:me)?|ver|leer?)\\s+)?"
            "(?:"
            "(?:cu[aá]l\\s+es\\s+(?:el\\s+|mi\\s+)?(?:contenido\\s+del?\\s+)?(?:portapapeles|texto\\s+copiado))"
            "|(?:(?:el\\s+|mi\\s+)?contenido\\s+del?\\s+portapapeles)"
            "|(?:qu[eé]\\s+(?:hay|tengo|contiene|dice|pone|est[aá]\\s+copiado|tenemos)\\s+(?:en\\s+(?:el|mi)\\s+portapapeles|el\\s+portapapeles|copiado))"
            "|(?:qu[eé]\\s+(?:he|tengo|hay|acabo\\s+de)\\s+copiado)"
            "|(?:(?:el|mi)\\s+)?portapapeles"
            ")[.!?¿¡]*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.contains(clipQueryRegex)) {
        const QString clip = ClipboardManager::instance().getCurrentClipboard();
        result.handled = true;
        if (!clip.isEmpty()) {
            const QString displayedClip = clip.length() > 4000
                ? (clip.left(4000) + QStringLiteral("…\n\n*(Texto truncado por longitud)*"))
                : clip;
            result.feedback = i18n("En el portapapeles tienes copiado:\n\n%1", displayedClip);
        } else {
            result.feedback = i18n("El portapapeles está vacío actualmente. No hay ningún texto copiado.");
        }
        return result;
    }

    return result;
}

} // namespace Kity
