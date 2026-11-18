/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>

namespace Kity {

struct AppInfo {
    QString id;
    QString name;
    QString genericName;
    QString exec;
    QString desktopEntryName;
    QString icon;
};

class ApplicationManager : public QObject
{
    Q_OBJECT

public:
    static ApplicationManager &instance();

    void reloadCatalog();
    QList<AppInfo> searchApps(const QString &query) const;

    bool launchApp(const QString &query, QString *launchedNameOut = nullptr);
    bool closeApp(const QString &query, QString *closedNameOut = nullptr);
    bool isAppRunning(const QString &query) const;

private:
    explicit ApplicationManager(QObject *parent = nullptr);
    ~ApplicationManager() override = default;

    ApplicationManager(const ApplicationManager &) = delete;
    ApplicationManager &operator=(const ApplicationManager &) = delete;

    static QString normalizeQuery(const QString &input);
    bool resolveSemanticAlias(const QString &cleanQuery, QString *launchedNameOut);

    QList<AppInfo> m_catalog;
};

} // namespace Kity
