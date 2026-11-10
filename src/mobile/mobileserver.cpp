/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "mobileserver.h"

#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

KityMobileServer::KityMobileServer(quint16 port, QObject *parent)
    : QObject(parent)
    , m_port(port)
{
    loadOrCreateAuthToken();
}

KityMobileServer::~KityMobileServer()
{
    stop();
}

void KityMobileServer::loadOrCreateAuthToken()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/kity");
    QDir().mkpath(configDir);

    const QString tokenFilePath = configDir + QStringLiteral("/mobile.token");
    QFile tokenFile(tokenFilePath);

    if (tokenFile.exists() && tokenFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_authToken = QString::fromUtf8(tokenFile.readAll()).trimmed();
        tokenFile.close();
    }

    if (m_authToken.length() < 32) {
        // Generate a 64-character secure hexadecimal token
        QByteArray randomBytes(32, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(randomBytes.data()),
                                              randomBytes.size() / sizeof(quint32));
        m_authToken = QString::fromLatin1(randomBytes.toHex());

        if (tokenFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            tokenFile.write(m_authToken.toUtf8());
            tokenFile.close();
            // Restrict permissions to owner only (0600)
            tokenFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        }
    }
}

bool KityMobileServer::isAllowedClient(const QHostAddress &address) const
{
    if (address.isLoopback() || address == QHostAddress::LocalHost || address == QHostAddress::LocalHostIPv6
        || address == QHostAddress(QStringLiteral("127.0.0.1")) || address == QHostAddress(QStringLiteral("::1"))) {
        return true;
    }

    const QString clientIpStr = address.toString();
    // Allow private LAN ranges (RFC 1918) and IPv4-mapped IPv6 (e.g. ::ffff:192.168.1.12)
    if (clientIpStr.contains(QStringLiteral("192.168.")) ||
        clientIpStr.contains(QStringLiteral("10.")) ||
        clientIpStr.contains(QStringLiteral("172."))) {
        return true;
    }

    return false;
}

bool KityMobileServer::start()
{
    if (m_running) {
        return true;
    }

    // 1. Initialize HTTP Static Server for PWA assets
    m_httpServer = new QTcpServer(this);
    connect(m_httpServer, &QTcpServer::newConnection, this, &KityMobileServer::onNewHttpConnection);

    if (!m_httpServer->listen(QHostAddress::AnyIPv4, m_port)) {
        m_httpServer->deleteLater();
        m_httpServer = nullptr;
        return false;
    }

    // 2. Initialize WebSocket Server for Real-Time Full-Duplex communication
    // We bind the WebSocket server to m_port + 1 to avoid port collisions with the HTTP server
    const quint16 wsPort = m_port + 1;
    m_wsServer = new QWebSocketServer(QStringLiteral("KityMobileWsServer"),
                                      QWebSocketServer::NonSecureMode,
                                      this);
    connect(m_wsServer, &QWebSocketServer::newConnection, this, &KityMobileServer::onNewWebSocketConnection);

    if (!m_wsServer->listen(QHostAddress::AnyIPv4, wsPort)) {
        m_httpServer->close();
        m_httpServer->deleteLater();
        m_httpServer = nullptr;

        m_wsServer->deleteLater();
        m_wsServer = nullptr;
        return false;
    }

    m_running = true;
    Q_EMIT serverStarted(m_port);
    return true;
}

void KityMobileServer::stop()
{
    if (!m_running) {
        return;
    }

    for (QWebSocket *client : std::as_const(m_wsClients)) {
        if (client) {
            client->close();
            client->deleteLater();
        }
    }
    m_wsClients.clear();

    if (m_wsServer) {
        m_wsServer->close();
        m_wsServer->deleteLater();
        m_wsServer = nullptr;
    }

    if (m_httpServer) {
        m_httpServer->close();
        m_httpServer->deleteLater();
        m_httpServer = nullptr;
    }

    m_running = false;
    Q_EMIT serverStopped();
}

bool KityMobileServer::isRunning() const
{
    return m_running;
}

quint16 KityMobileServer::port() const
{
    return m_port;
}

QString KityMobileServer::authToken() const
{
    return m_authToken;
}

QString KityMobileServer::localIpAddress() const
{
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const auto entries = iface.addressEntries();
        for (const auto &entry : entries) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                const QString ip = addr.toString();
                if (ip.startsWith(QLatin1String("192.168."))) {
                    return ip;
                }
            }
        }
    }

    return QStringLiteral("127.0.0.1");
}

QString KityMobileServer::pairingUrl() const
{
    return QStringLiteral("http://%1:%2/?token=%3")
        .arg(localIpAddress())
        .arg(m_port)
        .arg(m_authToken);
}

bool KityMobileServer::sendPairingUrlViaKdeConnect(const QString &deviceId) const
{
    if (deviceId.isEmpty()) {
        return false;
    }

    QDBusInterface shareIface(QStringLiteral("org.kde.kdeconnect"),
                              QStringLiteral("/modules/kdeconnect/devices/") + deviceId + QStringLiteral("/share"),
                              QStringLiteral("org.kde.kdeconnect.device.share"),
                              QDBusConnection::sessionBus());

    if (shareIface.isValid()) {
        shareIface.call(QStringLiteral("shareUrl"), pairingUrl());
        return true;
    }

    return false;
}

void KityMobileServer::onNewHttpConnection()
{
    if (!m_httpServer) {
        return;
    }

    QTcpSocket *socket = m_httpServer->nextPendingConnection();
    if (!socket) {
        return;
    }

    const QHostAddress peer = socket->peerAddress();
    if (!isAllowedClient(peer)) {
        sendHttpResponse(socket, 403, QStringLiteral("Forbidden"), QStringLiteral("text/plain"), "Acceso denegado: IP no autorizada.");
        socket->disconnectFromHost();
        return;
    }

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        processHttpRequest(socket);
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void KityMobileServer::processHttpRequest(QTcpSocket *socket)
{
    const QByteArray requestData = socket->readAll();
    const QString requestString = QString::fromUtf8(requestData);

    const QStringList lines = requestString.split(QStringLiteral("\r\n"));
    if (lines.isEmpty()) {
        return;
    }

    const QStringList requestLine = lines.first().split(QLatin1Char(' '));
    if (requestLine.size() < 2) {
        return;
    }

    const QString method = requestLine.at(0);
    const QString pathWithQuery = requestLine.at(1);
    const bool isHead = (method == QStringLiteral("HEAD"));

    if (method != QStringLiteral("GET") && !isHead) {
        sendHttpResponse(socket, 405, QStringLiteral("Method Not Allowed"), QStringLiteral("text/plain"), "Solo métodos GET y HEAD permitidos.");
        return;
    }

    const QUrl url(pathWithQuery);
    QString path = url.path();
    if (path == QStringLiteral("/") || path.isEmpty()) {
        path = QStringLiteral("/index.html");
    }

    // 1. Try static resource routing from Qt Embedded Resources (Zero Disk I/O)
    QString resourcePath = QStringLiteral(":/kity/mobile/web") + path;
    QFile file(resourcePath);

    // 2. Fallback to filesystem if not yet loaded in resource cache
    if (!file.exists()) {
        const QString devPath = QStringLiteral("/home/cesar/Proyectos/kiri/src/mobile/web") + path;
        if (QFile::exists(devPath)) {
            file.setFileName(devPath);
        } else {
            const QString installedPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                                 QStringLiteral("kity/mobile/web") + path);
            if (!installedPath.isEmpty()) {
                file.setFileName(installedPath);
            }
        }
    }

    if (!file.exists()) {
        sendHttpResponse(socket, 404, QStringLiteral("Not Found"), QStringLiteral("text/plain"), "Archivo no encontrado.");
        return;
    }

    QString contentType = QStringLiteral("application/octet-stream");
    if (path.endsWith(QStringLiteral(".html"))) {
        contentType = QStringLiteral("text/html; charset=utf-8");
    } else if (path.endsWith(QStringLiteral(".css"))) {
        contentType = QStringLiteral("text/css; charset=utf-8");
    } else if (path.endsWith(QStringLiteral(".js"))) {
        contentType = QStringLiteral("application/javascript; charset=utf-8");
    } else if (path.endsWith(QStringLiteral(".json"))) {
        contentType = QStringLiteral("application/manifest+json; charset=utf-8");
    } else if (path.endsWith(QStringLiteral(".svg"))) {
        contentType = QStringLiteral("image/svg+xml");
    }

    if (file.open(QIODevice::ReadOnly)) {
        QByteArray content = file.readAll();
        file.close();

        // Dynamically inject the WebSocket port into app.js so it connects to wsPort (m_port + 1)
        if (path == QStringLiteral("/app.js")) {
            const QString wsPortInject = QStringLiteral("window.KITY_WS_PORT = %1;\n").arg(m_port + 1);
            content.prepend(wsPortInject.toUtf8());
        }

        if (isHead) {
            sendHttpResponse(socket, 200, QStringLiteral("OK"), contentType, QByteArray());
        } else {
            sendHttpResponse(socket, 200, QStringLiteral("OK"), contentType, content);
        }
    } else {
        sendHttpResponse(socket, 500, QStringLiteral("Internal Server Error"), QStringLiteral("text/plain"), "Error al leer recurso.");
    }
}

void KityMobileServer::sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                                        const QString &contentType, const QByteArray &body)
{
    QByteArray response;
    response.append(QStringLiteral("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append(QStringLiteral("Content-Type: %1\r\n").arg(contentType).toUtf8());
    response.append(QStringLiteral("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("Connection: close\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void KityMobileServer::onNewWebSocketConnection()
{
    if (!m_wsServer) {
        return;
    }

    QWebSocket *clientSocket = m_wsServer->nextPendingConnection();
    if (!clientSocket) {
        return;
    }

    const QHostAddress peer = clientSocket->peerAddress();
    if (!isAllowedClient(peer)) {
        clientSocket->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("IP no autorizada"));
        clientSocket->deleteLater();
        return;
    }

    // Verify token from WebSocket query parameters
    const QUrl requestUrl = clientSocket->requestUrl();
    QUrlQuery query;
    if (requestUrl.hasQuery()) {
        query = QUrlQuery(requestUrl);
    } else {
        // If requestUrl only contains the path or raw string
        query = QUrlQuery(requestUrl.toString());
    }
    const QString token = query.queryItemValue(QStringLiteral("token"));

    if (token != m_authToken) {
        QJsonObject errObj;
        errObj.insert(QStringLiteral("type"), QStringLiteral("auth_error"));
        errObj.insert(QStringLiteral("text"), QStringLiteral("Token de seguridad inválido."));
        clientSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(errObj).toJson(QJsonDocument::Compact)));

        clientSocket->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("Token inválido"));
        clientSocket->deleteLater();
        return;
    }

    m_wsClients.append(clientSocket);
    Q_EMIT clientConnected(peer.toString());

    connect(clientSocket, &QWebSocket::textMessageReceived, this, &KityMobileServer::onWebSocketTextMessage);
    connect(clientSocket, &QWebSocket::disconnected, this, &KityMobileServer::onWebSocketDisconnected);

    // Send initial handshake state
    QJsonObject stateObj;
    stateObj.insert(QStringLiteral("type"), QStringLiteral("state"));
    stateObj.insert(QStringLiteral("state"), QStringLiteral("idle"));
    stateObj.insert(QStringLiteral("text"), QStringLiteral("Conectado con Kity en CachyOS"));
    clientSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(stateObj).toJson(QJsonDocument::Compact)));
}

void KityMobileServer::onWebSocketTextMessage(const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        return;
    }

    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("command")) {
        const QString text = obj.value(QStringLiteral("text")).toString().trimmed();
        const bool fromVoice = obj.value(QStringLiteral("fromVoice")).toBool(true);

        if (!text.isEmpty()) {
            Q_EMIT voiceCommandReceived(text, fromVoice);
        }
    }
}

void KityMobileServer::onWebSocketDisconnected()
{
    auto *clientSocket = qobject_cast<QWebSocket *>(sender());
    if (clientSocket) {
        m_wsClients.removeAll(clientSocket);
        Q_EMIT clientDisconnected(clientSocket->peerAddress().toString());
        clientSocket->deleteLater();
    }
}

void KityMobileServer::broadcastState(const QString &state, const QString &text)
{
    if (m_wsClients.isEmpty()) {
        return;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("state"));
    obj.insert(QStringLiteral("state"), state);
    if (!text.isEmpty()) {
        obj.insert(QStringLiteral("text"), text);
    }

    const QString payload = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    for (QWebSocket *client : std::as_const(m_wsClients)) {
        if (client && client->isValid()) {
            client->sendTextMessage(payload);
        }
    }
}

void KityMobileServer::broadcastAudioLevel(qreal level)
{
    if (m_wsClients.isEmpty()) {
        return;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("audioLevel"));
    obj.insert(QStringLiteral("level"), level);

    const QString payload = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    for (QWebSocket *client : std::as_const(m_wsClients)) {
        if (client && client->isValid()) {
            client->sendTextMessage(payload);
        }
    }
}

void KityMobileServer::broadcastStatus(const QString &status)
{
    if (m_wsClients.isEmpty()) {
        return;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("status"));
    obj.insert(QStringLiteral("text"), status);

    const QString payload = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    for (QWebSocket *client : std::as_const(m_wsClients)) {
        if (client && client->isValid()) {
            client->sendTextMessage(payload);
        }
    }
}
