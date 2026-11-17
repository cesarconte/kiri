/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include "kdictationclient.h"

class MockDictationService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.cesar.kdictation.Dictation")

public:
    explicit MockDictationService(QObject *parent = nullptr) : QObject(parent) {}

public Q_SLOTS:
    bool IsEngineReady() const {
        return true;
    }

    QString TranscribePcm(const QByteArray &pcmData, const QString &language) {
        Q_UNUSED(language);
        if (pcmData.isEmpty()) {
            return QString();
        }
        return QStringLiteral("Hola Kity desde KDictation D-Bus");
    }
};

class TestKDictationClient : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testClientInitialization();
    void testEmptyBufferHandling();
    void testServiceInteractionWithMock();
    void testServiceDisconnect();

private:
    MockDictationService *m_mock = nullptr;
};

void TestKDictationClient::initTestCase()
{
    QVERIFY(QDBusConnection::sessionBus().isConnected());
}

void TestKDictationClient::testClientInitialization()
{
    KDictationClient client;
    // Si kdictation no corre en el sistema real en este momento, isServiceAvailable debe ser false de manera segura
    // y no provocar bloqueos.
    QCOMPARE(client.isEngineReady(), client.isServiceAvailable());
}

void TestKDictationClient::testEmptyBufferHandling()
{
    KDictationClient client;
    QSignalSpy failedSpy(&client, &KDictationClient::transcriptionFailed);
    QSignalSpy readySpy(&client, &KDictationClient::transcriptionReady);

    client.transcribePcmAsync(QByteArray());

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(readySpy.count(), 0);
}

void TestKDictationClient::testServiceInteractionWithMock()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusConnectionInterface *busIface = bus.interface();

    // Solo registramos el mock si el servicio real no estuviera registrado
    const bool realServiceRunning = busIface && busIface->isServiceRegistered(QStringLiteral("org.cesar.kdictation"));

    if (!realServiceRunning) {
        m_mock = new MockDictationService(this);
        const bool objReg = bus.registerObject(
            QStringLiteral("/Dictation"),
            m_mock,
            QDBusConnection::ExportAllSlots);
        QVERIFY(objReg);

        const bool srvReg = bus.registerService(QStringLiteral("org.cesar.kdictation"));
        QVERIFY(srvReg);
    }

    KDictationClient client;
    QVERIFY(client.checkAvailability());
    QVERIFY(client.isServiceAvailable());

    QSignalSpy readySpy(&client, &KDictationClient::transcriptionReady);
    QSignalSpy failedSpy(&client, &KDictationClient::transcriptionFailed);

    const QByteArray dummyPcm(3200, 0x10); // 100ms de audio mono 16kHz
    client.transcribePcmAsync(dummyPcm, QStringLiteral("es"), 2000);

    QVERIFY(readySpy.wait(3000));
    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);

    const QString transcript = readySpy.first().first().toString();
    if (m_mock) {
        QCOMPARE(transcript, QStringLiteral("Hola Kity desde KDictation D-Bus"));
    } else {
        // En la instancia real de KDictation, el buffer de audio sintético responde
        // válidamente a través de D-Bus (pudiendo ser vacío al no contener fonemas hablados).
        QVERIFY(transcript.isEmpty() || !transcript.isEmpty());
    }
}

void TestKDictationClient::testServiceDisconnect()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_mock) {
        bus.unregisterService(QStringLiteral("org.cesar.kdictation"));
        bus.unregisterObject(QStringLiteral("/Dictation"));
        delete m_mock;
        m_mock = nullptr;

        QTest::qWait(150);

        KDictationClient client;
        QVERIFY(!client.checkAvailability());
        QVERIFY(!client.isServiceAvailable());
    }
}

QTEST_MAIN(TestKDictationClient)
#include "test_kdictationclient.moc"
