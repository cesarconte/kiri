/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Kity {

class VisionEngine : public QObject
{
    Q_OBJECT

public:
    static VisionEngine &instance();

    QString captureScreenToPng(const QString &destPath, const QString &captureMode = QStringLiteral("fullscreen")) const;
    QString encodeImageToBase64(const QString &imagePath, int maxDimension = 1024) const;
    QString extractScreenOcrText(const QString &imagePath) const;
    static QVariantMap parseLiveTextEntities(const QString &ocrText);
    QString detectAvailableVisionModel() const;

private:
    explicit VisionEngine(QObject *parent = nullptr);
    ~VisionEngine() override = default;

    VisionEngine(const VisionEngine &) = delete;
    VisionEngine &operator=(const VisionEngine &) = delete;
};

} // namespace Kity
