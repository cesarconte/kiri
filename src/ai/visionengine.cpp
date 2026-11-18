/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "visionengine.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

namespace Kity {

VisionEngine &VisionEngine::instance()
{
    static VisionEngine s_instance;
    return s_instance;
}

VisionEngine::VisionEngine(QObject *parent)
    : QObject(parent)
{
}

QString VisionEngine::captureScreenToPng(const QString &destPath, const QString &captureMode) const
{
    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (!spectacle.isEmpty()) {
        QFile::remove(destPath);
        QString modeFlag = QStringLiteral("-f");
        int timeoutMs = 4500;
        if (captureMode == QStringLiteral("region")) {
            modeFlag = QStringLiteral("-r");
            timeoutMs = 25000; // Allow user time to draw rectangular selection on Wayland
        } else if (captureMode == QStringLiteral("activewindow")) {
            modeFlag = QStringLiteral("-a");
            timeoutMs = 4500;
        }

        QProcess proc;
        proc.start(spectacle, {modeFlag, QStringLiteral("-b"), QStringLiteral("-n"), QStringLiteral("-o"), destPath});

        // Use a non-blocking QEventLoop so D-Bus loop remains responsive
        // and wait until Spectacle finishes flushing the PNG to disk.
        QEventLoop loop;
        QTimer pollTimer;
        QElapsedTimer deadline;
        deadline.start();

        connect(&proc, &QProcess::finished, &loop, [&](int, QProcess::ExitStatus) {
            if (captureMode == QStringLiteral("region") && !QFileInfo::exists(destPath)) {
                // User cancelled rectangular selection (Escape)
                loop.quit();
            }
        });

        connect(&pollTimer, &QTimer::timeout, &loop, [&]() {
            if (QFileInfo::exists(destPath) && QFileInfo(destPath).size() > 512) {
                QImage testImg(destPath);
                if (!testImg.isNull()) {
                    loop.quit();
                    return;
                }
            }
            if (deadline.elapsed() > timeoutMs) {
                loop.quit();
            }
        });
        pollTimer.start(50);
        loop.exec();

        if (proc.state() != QProcess::NotRunning) {
            proc.waitForFinished(500);
        }

        if (QFileInfo::exists(destPath) && QFileInfo(destPath).size() > 0) {
            return destPath;
        }
    }
    return QString();
}

QString VisionEngine::encodeImageToBase64(const QString &imagePath, int maxDimension) const
{
    QImage img(imagePath);
    if (img.isNull()) return QString();

    if (img.width() > maxDimension || img.height() > maxDimension) {
        img = img.scaled(maxDimension, maxDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "JPEG", 85);
    return QString::fromLatin1(bytes.toBase64());
}

QString VisionEngine::extractScreenOcrText(const QString &imagePath) const
{
    if (imagePath.isEmpty() || !QFileInfo::exists(imagePath)) {
        return QString();
    }

    const QString tesseract = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (tesseract.isEmpty()) {
        return QString();
    }

    QProcess proc;
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(&proc, &QProcess::finished, &loop, &QEventLoop::quit);

    proc.start(tesseract, {imagePath, QStringLiteral("stdout"), QStringLiteral("-l"), QStringLiteral("spa+eng"), QStringLiteral("--psm"), QStringLiteral("3")});
    timeoutTimer.start(6000);
    loop.exec();

    if (proc.state() != QProcess::NotRunning) {
        proc.kill();
        proc.waitForFinished(200);
    }

    const QString rawOcr = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList cleanLines;
    const QStringList lines = rawOcr.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) {
            cleanLines << t;
        }
    }

    return cleanLines.join(QLatin1Char('\n')).left(4000).trimmed();
}

QVariantMap VisionEngine::parseLiveTextEntities(const QString &ocrText)
{
    QVariantMap res;
    QStringList urls;
    QStringList emails;
    QStringList phones;

    static const QRegularExpression urlRx(QStringLiteral("(?:https?://|www\\.)[\\w\\-._~:/?#\\[\\]@!$&'()*+,;=%]+"), QRegularExpression::CaseInsensitiveOption);
    auto urlIt = urlRx.globalMatch(ocrText);
    while (urlIt.hasNext()) {
        QString u = urlIt.next().captured(0);
        if (u.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) {
            u.prepend(QStringLiteral("https://"));
        }
        if (!urls.contains(u)) urls.append(u);
    }

    static const QRegularExpression emailRx(QStringLiteral("[a-zA-Z0-9._%+\\-]+@[a-zA-Z0-9.\\-]+\\.[a-zA-Z]{2,}"), QRegularExpression::CaseInsensitiveOption);
    auto emailIt = emailRx.globalMatch(ocrText);
    while (emailIt.hasNext()) {
        const QString e = emailIt.next().captured(0);
        if (!emails.contains(e)) emails.append(e);
    }

    static const QRegularExpression phoneRx(QStringLiteral("(?:\\+\\d{1,3}[\\s\\-]?)?(?:(?:\\(?\\d{2,4}\\)?[\\s\\-]?)?\\d{3}[\\s\\-]?\\d{2,4}[\\s\\-]?\\d{2,4})"));
    auto phoneIt = phoneRx.globalMatch(ocrText);
    while (phoneIt.hasNext()) {
        const QString p = phoneIt.next().captured(0).trimmed();
        if (p.length() >= 9 && !phones.contains(p)) phones.append(p);
    }

    res[QStringLiteral("urls")] = urls;
    res[QStringLiteral("emails")] = emails;
    res[QStringLiteral("phones")] = phones;
    return res;
}

QString VisionEngine::detectAvailableVisionModel() const
{
    const QStringList knownVision = {
        QStringLiteral("llama3.2-vision"),
        QStringLiteral("llava"),
        QStringLiteral("bakllava"),
        QStringLiteral("minicpm-v"),
        QStringLiteral("qwen2.5vl")
    };
    return knownVision.first();
}

} // namespace Kity
