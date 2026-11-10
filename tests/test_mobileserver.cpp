/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "mobileserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>
#include <QWebSocket>

class TestMobileServer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        Q_INIT_RESOURCE(mobile_resources);
    }
    void testTokenGenerationAndAttributes()
    {
        KityMobileServer server(8990);
        QVERIFY(server.authToken().length() >= 32);
        QVERIFY(!server.localIpAddress().isEmpty());
        const QString pairingUrl = server.pairingUrl();
        QVERIFY(pairingUrl.startsWith(QStringLiteral("http://")));
        QVERIFY(pairingUrl.contains(QStringLiteral(":8990/?token=")));
        QVERIFY(pairingUrl.contains(server.authToken()));
    }

    void testServerLifecycle()
    {
        KityMobileServer server(8992);
        QVERIFY(!server.isRunning());

        QVERIFY(server.start());
        QVERIFY(server.isRunning());

        // Calling start again should be a no-op returning true
        QVERIFY(server.start());

        server.stop();
        QVERIFY(!server.isRunning());
    }

    void testHttpStaticServing()
    {
        KityMobileServer server(8994);
        QVERIFY(server.start());

        QNetworkAccessManager nam;
        QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:8994/")));
        QNetworkReply *reply = nam.get(req);

        QSignalSpy spy(reply, &QNetworkReply::finished);
        QVERIFY(spy.wait(2000));

        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        const QByteArray body = reply->readAll();
        QVERIFY(body.contains("Kity"));
        QVERIFY(body.contains("screenGlow"));
        reply->deleteLater();

        server.stop();
    }

    void testWebSocketUnauthorizedRejection()
    {
        KityMobileServer server(8996);
        QVERIFY(server.start());

        // wsPort is m_port + 1 = 8997
        QWebSocket ws;
        QSignalSpy disconnectedSpy(&ws, &QWebSocket::disconnected);
        ws.open(QUrl(QStringLiteral("ws://127.0.0.1:8997/ws?token=INVALID_TOKEN_XYZ")));

        // Server should reject and disconnect
        QVERIFY(disconnectedSpy.wait(2000));

        server.stop();
    }

    void testWebSocketAuthorizedCommunication()
    {
        KityMobileServer server(8998);
        QVERIFY(server.start());

        QWebSocket ws;
        QSignalSpy connectedSpy(&ws, &QWebSocket::connected);
        QSignalSpy messageReceivedSpy(&ws, &QWebSocket::textMessageReceived);
        QSignalSpy commandSpy(&server, &KityMobileServer::voiceCommandReceived);

        const QString wsUrl = QStringLiteral("ws://127.0.0.1:8999/ws?token=%1").arg(server.authToken());
        ws.open(QUrl(wsUrl));

        QVERIFY(connectedSpy.wait(2000));

        // Wait for initial handshake state message if not already received during connection
        if (messageReceivedSpy.isEmpty()) {
            QVERIFY(messageReceivedSpy.wait(2000));
        }
        QVERIFY(!messageReceivedSpy.isEmpty());
        const QString firstMsg = messageReceivedSpy.takeFirst().at(0).toString();
        const QJsonDocument doc = QJsonDocument::fromJson(firstMsg.toUtf8());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value(QStringLiteral("type")).toString(), QStringLiteral("state"));

        // Send a voice command from the mobile client
        QJsonObject cmdObj;
        cmdObj.insert(QStringLiteral("type"), QStringLiteral("command"));
        cmdObj.insert(QStringLiteral("text"), QStringLiteral("Hola Kity"));
        cmdObj.insert(QStringLiteral("fromVoice"), true);
        ws.sendTextMessage(QString::fromUtf8(QJsonDocument(cmdObj).toJson(QJsonDocument::Compact)));

        QVERIFY(commandSpy.wait(2000));
        QCOMPARE(commandSpy.count(), 1);
        const QList<QVariant> args = commandSpy.first();
        QCOMPARE(args.at(0).toString(), QStringLiteral("Hola Kity"));
        QCOMPARE(args.at(1).toBool(), true);

        // Test server broadcast
        server.broadcastState(QStringLiteral("speaking"), QStringLiteral("Respondiendo..."));
        QVERIFY(messageReceivedSpy.wait(2000));
        const QString broadcastMsg = messageReceivedSpy.takeFirst().at(0).toString();
        const QJsonDocument bDoc = QJsonDocument::fromJson(broadcastMsg.toUtf8());
        QCOMPARE(bDoc.object().value(QStringLiteral("state")).toString(), QStringLiteral("speaking"));

        ws.close();
        server.stop();
    }
};

QTEST_MAIN(TestMobileServer)
#include "test_mobileserver.moc"
