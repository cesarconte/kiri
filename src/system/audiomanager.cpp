/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "audiomanager.h"

#include <KLocalizedString>
#include <QProcess>
#include <QRegularExpression>
#include <QtGlobal>

namespace Kity {

AudioManager &AudioManager::instance()
{
    static AudioManager s_instance;
    return s_instance;
}

AudioManager::AudioManager(QObject *parent)
    : QObject(parent)
{
}

QString AudioManager::getAudioVolumeStatus() const
{
    QProcess proc;
    proc.start(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        static const QRegularExpression rx(QStringLiteral("Volume:\\s*([0-9.]+)(?:\\s*\\[MUTED\\])?"));
        const auto m = rx.match(out);
        if (m.hasMatch()) {
            const double vol = m.captured(1).toDouble();
            const int pct = qRound(vol * 100.0);
            const bool isMuted = out.contains(QStringLiteral("[MUTED]"), Qt::CaseInsensitive);
            if (isMuted) {
                return i18n("El volumen está al %1%, pero el audio está silenciado.", pct);
            }
            return i18n("El volumen actual es del %1%.", pct);
        }
    }
    return i18n("No se pudo obtener el nivel de volumen actual.");
}

int AudioManager::getVolumePercent() const
{
    QProcess proc;
    proc.start(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        static const QRegularExpression rx(QStringLiteral("Volume:\\s*([0-9.]+)"));
        const auto m = rx.match(out);
        if (m.hasMatch()) {
            return qRound(m.captured(1).toDouble() * 100.0);
        }
    }
    return -1;
}

bool AudioManager::isAudioMuted() const
{
    QProcess proc;
    proc.start(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        return out.contains(QStringLiteral("[MUTED]"), Qt::CaseInsensitive);
    }
    return false;
}

bool AudioManager::setVolume(int percentage)
{
    const int pct = qBound(0, percentage, 150);
    return QProcess::execute(QStringLiteral("wpctl"),
        {QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("%1%").arg(pct)}) == 0;
}

bool AudioManager::stepVolume(int deltaPercentage)
{
    const QString step = QStringLiteral("%1%%2").arg(qAbs(deltaPercentage)).arg(deltaPercentage >= 0 ? QStringLiteral("+") : QStringLiteral("-"));
    return QProcess::execute(QStringLiteral("wpctl"),
        {QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), step}) == 0;
}

bool AudioManager::setAudioMute(bool mute)
{
    return QProcess::execute(QStringLiteral("wpctl"),
        {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), mute ? QStringLiteral("1") : QStringLiteral("0")}) == 0;
}

QString AudioManager::getMicrophoneStatus() const
{
    QProcess proc;
    proc.start(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SOURCE@")});
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        static const QRegularExpression rx(QStringLiteral("Volume:\\s*([0-9.]+)(?:\\s*\\[MUTED\\])?"));
        const auto m = rx.match(out);
        if (m.hasMatch()) {
            const double vol = m.captured(1).toDouble();
            const int pct = qRound(vol * 100.0);
            const bool isMuted = out.contains(QStringLiteral("[MUTED]"), Qt::CaseInsensitive);
            if (isMuted) {
                return i18n("El micrófono está silenciado (nivel configurado al %1%).", pct);
            }
            return i18n("El micrófono está activo con un nivel del %1%.", pct);
        }
    }
    return i18n("No se pudo consultar el estado del micrófono.");
}

int AudioManager::getMicrophonePercent() const
{
    QProcess proc;
    proc.start(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SOURCE@")});
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        static const QRegularExpression rx(QStringLiteral("Volume:\\s*([0-9.]+)"));
        const auto m = rx.match(out);
        if (m.hasMatch()) {
            return qRound(m.captured(1).toDouble() * 100.0);
        }
    }
    return -1;
}

bool AudioManager::isMicrophoneMuted() const
{
    QProcess proc;
    proc.start(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SOURCE@")});
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        return out.contains(QStringLiteral("[MUTED]"), Qt::CaseInsensitive);
    }
    return false;
}

bool AudioManager::setMicrophoneMute(bool mute)
{
    return QProcess::execute(QStringLiteral("wpctl"),
        {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SOURCE@"), mute ? QStringLiteral("1") : QStringLiteral("0")}) == 0;
}

bool AudioManager::toggleEasyEffectsBypass()
{
    return QProcess::startDetached(QStringLiteral("easyeffects"), {QStringLiteral("--bypass-toggle")});
}

bool AudioManager::loadEasyEffectsPreset(const QString &presetName)
{
    if (presetName.trimmed().isEmpty()) return false;
    return QProcess::startDetached(QStringLiteral("easyeffects"), {QStringLiteral("-l"), presetName.trimmed()});
}

} // namespace Kity
