/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QList>

namespace Kity {

struct WindowInfo {
    QString caption;
    QString resourceClass;
    bool active = false;
    bool minimized = false;
    qint64 pid = 0;
};

class WindowManager : public QObject
{
    Q_OBJECT

public:
    static WindowManager &instance();

    QList<WindowInfo> listWindows() const;
    QString windowsSummary() const;

    bool closeWindow(const QString &query, QString *closedCaptionOut = nullptr);
    bool activateWindow(const QString &query, QString *activatedCaptionOut = nullptr);
    bool minimizeWindow(const QString &query = QString());
    bool maximizeWindow(const QString &query = QString());
    bool showDesktop();

private:
    explicit WindowManager(QObject *parent = nullptr);
    ~WindowManager() override = default;

    WindowManager(const WindowManager &) = delete;
    WindowManager &operator=(const WindowManager &) = delete;

    bool runKWinScript(const QString &jsCode, QString *scriptOutputOut = nullptr) const;
    static QString normalizeQuery(const QString &input);
};

} // namespace Kity
