/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocket>
#include <QWebSocketServer>

class KityMobileServer : public QObject
{
    Q_OBJECT

public:
    explicit KityMobileServer(quint16 port = 1740, QObject *parent = nullptr);
    ~KityMobileServer() override;

    bool start();
    void stop();
    bool isRunning() const;

    quint16 port() const;
    QString authToken() const;
    QString localIpAddress() const;
    QString pairingUrl() const;

    bool sendPairingUrlViaKdeConnect(const QString &deviceId) const;

public Q_SLOTS:
    void broadcastState(const QString &state, const QString &text = QString());
    void broadcastAudioLevel(qreal level);
    void broadcastStatus(const QString &status);

Q_SIGNALS:
    void voiceCommandReceived(const QString &text, bool fromVoice);
    void serverStarted(quint16 port);
    void serverStopped();
    void clientConnected(const QString &peerAddress);
    void clientDisconnected(const QString &peerAddress);

private Q_SLOTS:
    void onNewHttpConnection();
    void onNewWebSocketConnection();
    void onWebSocketTextMessage(const QString &message);
    void onWebSocketDisconnected();

private:
    void loadOrCreateAuthToken();
    bool isAllowedClient(const QHostAddress &address) const;
    void processHttpRequest(QTcpSocket *socket);
    void sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                          const QString &contentType, const QByteArray &body);

    quint16 m_port = 8765;
    QTcpServer *m_httpServer = nullptr;
    QWebSocketServer *m_wsServer = nullptr;
    QList<QWebSocket *> m_wsClients;
    QString m_authToken;
    bool m_running = false;
};
