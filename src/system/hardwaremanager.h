/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace Kity {

struct RamInfo {
    double totalGiB = 0.0;
    double usedGiB = 0.0;
    double availGiB = 0.0;
    int usagePercent = 0;
};

struct DiskInfo {
    double totalGiB = 0.0;
    double usedGiB = 0.0;
    double freeGiB = 0.0;
};

struct CpuLoad {
    double load1 = 0.0;
    double load5 = 0.0;
    double load15 = 0.0;
};

/**
 * @brief HardwareManager handles system hardware vitals, resource usage statistics,
 * and display controls (e.g. brightness via Solid PowerManagement D-Bus).
 */
class HardwareManager
{
public:
    static HardwareManager &instance();

    // Raw metrics
    RamInfo getRamInfo() const;
    DiskInfo getDiskInfo(const QString &mountPoint = QStringLiteral("/")) const;
    CpuLoad getCpuLoad() const;
    QString getLocalIpAddress() const;

    // Human-readable summaries & context prompts
    QString collectSystemVitals() const;
    QString getSystemResourceSummary(const QString &aspect = QStringLiteral("all")) const;
    QJsonObject getVitalsJson() const;

    // Screen Brightness (KDE Solid PowerManagement D-Bus)
    int getScreenBrightnessPercent() const;
    bool adjustScreenBrightness(int deltaPercent, int exactPercent = -1) const;

private:
    HardwareManager() = default;
    ~HardwareManager() = default;
    HardwareManager(const HardwareManager &) = delete;
    HardwareManager &operator=(const HardwareManager &) = delete;
};

} // namespace Kity
