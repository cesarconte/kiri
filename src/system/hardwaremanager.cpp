/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "hardwaremanager.h"
#include "devicemanager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QSysInfo>
#include <KLocalizedString>

namespace Kity {

HardwareManager &HardwareManager::instance()
{
    static HardwareManager s_instance;
    return s_instance;
}

RamInfo HardwareManager::getRamInfo() const
{
    RamInfo info;
    QFile memFile(QStringLiteral("/proc/meminfo"));
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qint64 memTotalKb = 0;
        qint64 memAvailKb = 0;
        const QString content = QString::fromUtf8(memFile.readAll());
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("MemTotal:"))) {
                memTotalKb = line.simplified().section(QLatin1Char(' '), 1, 1).toLongLong();
            } else if (line.startsWith(QStringLiteral("MemAvailable:"))) {
                memAvailKb = line.simplified().section(QLatin1Char(' '), 1, 1).toLongLong();
            }
            if (memTotalKb > 0 && memAvailKb > 0) {
                break;
            }
        }
        if (memTotalKb > 0) {
            info.totalGiB = memTotalKb / (1024.0 * 1024.0);
            info.availGiB = memAvailKb / (1024.0 * 1024.0);
            info.usedGiB = qMax(0.0, info.totalGiB - info.availGiB);
            info.usagePercent = qBound(0, static_cast<int>(qRound((info.usedGiB / info.totalGiB) * 100.0)), 100);
        }
    }
    return info;
}

DiskInfo HardwareManager::getDiskInfo(const QString &mountPoint) const
{
    DiskInfo info;
    const QStorageInfo storage(mountPoint);
    if (storage.isValid() && storage.isReady()) {
        info.totalGiB = storage.bytesTotal() / (1024.0 * 1024.0 * 1024.0);
        info.freeGiB = storage.bytesAvailable() / (1024.0 * 1024.0 * 1024.0);
        info.usedGiB = qMax(0.0, info.totalGiB - info.freeGiB);
    }
    return info;
}

CpuLoad HardwareManager::getCpuLoad() const
{
    CpuLoad load;
    QFile loadavg(QStringLiteral("/proc/loadavg"));
    if (loadavg.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString lLine = QString::fromUtf8(loadavg.readAll()).trimmed();
        const QStringList parts = lLine.split(QLatin1Char(' '));
        if (parts.size() >= 3) {
            load.load1 = parts.at(0).toDouble();
            load.load5 = parts.at(1).toDouble();
            load.load15 = parts.at(2).toDouble();
        } else if (!parts.isEmpty()) {
            load.load1 = parts.at(0).toDouble();
        }
    }
    return load;
}

QString HardwareManager::getLocalIpAddress() const
{
    QString localIp = i18n("No conectada");
    QProcess ipProc;
    ipProc.start(QStringLiteral("hostname"), {QStringLiteral("-i")});
    if (ipProc.waitForFinished(800) && ipProc.exitCode() == 0) {
        const QString rawIp = QString::fromUtf8(ipProc.readAllStandardOutput()).trimmed();
        if (!rawIp.isEmpty()) {
            localIp = rawIp.section(QLatin1Char(' '), 0, 0);
        }
    }
    return localIp;
}

QString HardwareManager::collectSystemVitals() const
{
    QStringList vitals;
    vitals << QStringLiteral("SO: %1 (Kernel: %2, KDE Plasma 6, Wayland)")
              .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
    vitals << QStringLiteral("Host: %1 (%2)").arg(QSysInfo::machineHostName(), QSysInfo::currentCpuArchitecture());
    vitals << QStringLiteral("Gestor de paquetes: pacman, paru (AUR)");
    vitals << QStringLiteral("Terminal: Konsole");

    const RamInfo ram = getRamInfo();
    if (ram.totalGiB > 0.0) {
        vitals << QStringLiteral("RAM: %1 GB libres de %2 GB").arg(ram.availGiB, 0, 'f', 1).arg(ram.totalGiB, 0, 'f', 1);
    }

    const CpuLoad cpu = getCpuLoad();
    vitals << QStringLiteral("Carga CPU (1 min): %1").arg(QString::number(cpu.load1, 'f', 2));

    const QString pProfile = DeviceManager::instance().getActivePowerProfile();
    if (!pProfile.isEmpty()) {
        vitals << QStringLiteral("Perfil energía: %1").arg(pProfile);
    }

    return vitals.join(QStringLiteral(", "));
}

QString HardwareManager::getSystemResourceSummary(const QString &aspect) const
{
    const RamInfo ram = getRamInfo();
    if (aspect == QStringLiteral("ram")) {
        return i18n("Estás usando %1 GiB de %2 GiB de memoria RAM (%3% en uso, %4 GiB disponibles).",
                    QString::number(ram.usedGiB, 'f', 1),
                    QString::number(ram.totalGiB, 'f', 1),
                    ram.usagePercent,
                    QString::number(ram.availGiB, 'f', 1));
    }

    const DiskInfo disk = getDiskInfo(QStringLiteral("/"));
    if (aspect == QStringLiteral("disk")) {
        return i18n("Tienes %1 GiB libres de un total de %2 GiB en la partición principal (%3 GiB ocupados).",
                    QString::number(disk.freeGiB, 'f', 1),
                    QString::number(disk.totalGiB, 'f', 1),
                    QString::number(disk.usedGiB, 'f', 1));
    }

    const QString localIp = getLocalIpAddress();
    if (aspect == QStringLiteral("ip")) {
        return i18n("Tu dirección IP local es %1.", localIp);
    }

    const QString kernel = QSysInfo::kernelVersion();
    const QString distro = QSysInfo::prettyProductName();
    return i18n("**Estado del sistema (%1):**\n"
                "- **Kernel:** Linux `%2`\n"
                "- **Memoria RAM:** %3 GiB / %4 GiB (%5% en uso)\n"
                "- **Disco principal:** %6 GiB libres de %7 GiB\n"
                "- **IP local:** `%8`",
                distro,
                kernel,
                QString::number(ram.usedGiB, 'f', 1),
                QString::number(ram.totalGiB, 'f', 1),
                ram.usagePercent,
                QString::number(disk.freeGiB, 'f', 1),
                QString::number(disk.totalGiB, 'f', 1),
                localIp);
}

QJsonObject HardwareManager::getVitalsJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("os")] = QSysInfo::prettyProductName();
    obj[QStringLiteral("kernel")] = QSysInfo::kernelVersion();
    obj[QStringLiteral("host")] = QSysInfo::machineHostName();
    obj[QStringLiteral("cpu_arch")] = QSysInfo::currentCpuArchitecture();

    const RamInfo ram = getRamInfo();
    obj[QStringLiteral("ram_total_gib")] = ram.totalGiB;
    obj[QStringLiteral("ram_used_gib")] = ram.usedGiB;
    obj[QStringLiteral("ram_avail_gib")] = ram.availGiB;
    obj[QStringLiteral("ram_usage_percent")] = ram.usagePercent;

    const CpuLoad cpu = getCpuLoad();
    obj[QStringLiteral("cpu_load_1m")] = cpu.load1;
    obj[QStringLiteral("cpu_load_5m")] = cpu.load5;
    obj[QStringLiteral("cpu_load_15m")] = cpu.load15;

    const DiskInfo disk = getDiskInfo(QStringLiteral("/"));
    obj[QStringLiteral("disk_total_gib")] = disk.totalGiB;
    obj[QStringLiteral("disk_used_gib")] = disk.usedGiB;
    obj[QStringLiteral("disk_free_gib")] = disk.freeGiB;

    obj[QStringLiteral("local_ip")] = getLocalIpAddress();
    obj[QStringLiteral("power_profile")] = DeviceManager::instance().getActivePowerProfile();
    obj[QStringLiteral("battery_info")] = DeviceManager::instance().getLocalBatteryInfo();

    return obj;
}

int HardwareManager::getScreenBrightnessPercent() const
{
    QDBusInterface iface(QStringLiteral("org.kde.Solid.PowerManagement"),
                         QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
                         QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        const QDBusReply<int> maxReply = iface.call(QStringLiteral("brightnessMax"));
        const QDBusReply<int> curReply = iface.call(QStringLiteral("brightness"));
        if (maxReply.isValid() && maxReply.value() > 0 && curReply.isValid()) {
            return qBound(0, static_cast<int>(qRound((curReply.value() * 100.0) / maxReply.value())), 100);
        }
    }
    return -1;
}

bool HardwareManager::adjustScreenBrightness(int deltaPercent, int exactPercent) const
{
    QDBusInterface iface(QStringLiteral("org.kde.Solid.PowerManagement"),
                         QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
                         QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return false;
    }

    const QDBusReply<int> maxReply = iface.call(QStringLiteral("brightnessMax"));
    const QDBusReply<int> curReply = iface.call(QStringLiteral("brightness"));
    if (!maxReply.isValid() || maxReply.value() <= 0) {
        return false;
    }

    const int maxVal = maxReply.value();
    const int curVal = curReply.isValid() ? curReply.value() : (maxVal / 2);
    int targetVal = curVal;

    if (exactPercent >= 0) {
        targetVal = qBound(1, (maxVal * exactPercent) / 100, maxVal);
    } else {
        targetVal = qBound(1, curVal + (maxVal * deltaPercent) / 100, maxVal);
    }

    iface.call(QStringLiteral("setBrightness"), targetVal);
    return true;
}

} // namespace Kity
