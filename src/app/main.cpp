/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPainterPath>
#include <QQuickWindow>
#include <KDBusService>
#include <KLocalizedString>
#include <KLocalizedContext>
#include <KWindowEffects>

#include "kityclient.h"

int main(int argc, char *argv[])
{
    KLocalizedString::setApplicationDomain("plasma_applet_org.cesar.kity");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kity_app"));
    app.setOrganizationName(QStringLiteral("cesar"));
    app.setOrganizationDomain(QStringLiteral("cesar.org"));
    app.setApplicationDisplayName(QStringLiteral("Kity"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("org.cesar.kity")));
    app.setDesktopFileName(QStringLiteral("org.cesar.kity"));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Asistente conversacional de inteligencia artificial estilo Siri"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption listenOption(QStringList() << QStringLiteral("l") << QStringLiteral("listen"),
                                    i18n("Empezar a escuchar por voz inmediatamente al abrir"));
    parser.addOption(listenOption);

    QCommandLineOption ocrOption(QStringList() << QStringLiteral("o") << QStringLiteral("ocr"),
                                 i18n("Iniciar captura de pantalla inteligente Live Text (OCR)"));
    parser.addOption(ocrOption);

    QCommandLineOption newChatOption(QStringList() << QStringLiteral("n") << QStringLiteral("new-chat"),
                                     i18n("Iniciar una nueva conversación limpia"));
    parser.addOption(newChatOption);

    QCommandLineOption toggleOption(QStringList() << QStringLiteral("t") << QStringLiteral("toggle"),
                                    i18n("Alternar visibilidad de la ventana de Kity"));
    parser.addOption(toggleOption);

    parser.process(app);

    KDBusService service(KDBusService::Unique);

    KityClient client;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));

    engine.rootContext()->setContextProperty(QStringLiteral("kityClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("Plasmoid"), &client);

    engine.load(QUrl(QStringLiteral("qrc:/qml/app/qml/MainWindow.qml")));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    QObject *rootObj = engine.rootObjects().first();
    QQuickWindow *window = qobject_cast<QQuickWindow *>(rootObj);
    if (window) {
        auto updateBlurRegion = [window]() {
            QPainterPath path;
            path.addRoundedRect(QRectF(0, 0, window->width(), window->height()), 22, 22);
            KWindowEffects::enableBlurBehind(window, true, QRegion(path.toFillPolygon().toPolygon()));
        };
        updateBlurRegion();
        QObject::connect(window, &QWindow::widthChanged, window, updateBlurRegion);
        QObject::connect(window, &QWindow::heightChanged, window, updateBlurRegion);
    }

    auto handleCommandLine = [&](const QCommandLineParser &p) {
        if (p.isSet(newChatOption)) {
            client.clearHistory();
        }
        if (p.isSet(listenOption)) {
            client.startRecording();
        }
        if (p.isSet(ocrOption)) {
            client.runVisualIntelligence(QStringLiteral("region"), QStringLiteral("ocr"));
        }
        if (window) {
            if (p.isSet(toggleOption)) {
                if (window->isVisible() && window->visibility() != QWindow::Minimized) {
                    window->hide();
                } else {
                    window->show();
                    window->raise();
                    window->requestActivate();
                }
            } else {
                window->show();
                window->raise();
                window->requestActivate();
            }
        }
    };

    handleCommandLine(parser);

    QObject::connect(&service, &KDBusService::activateRequested,
                     [&](const QStringList &arguments, const QString &workingDirectory) {
        Q_UNUSED(workingDirectory)
        QCommandLineParser secondParser;
        secondParser.addOption(listenOption);
        secondParser.addOption(ocrOption);
        secondParser.addOption(newChatOption);
        secondParser.addOption(toggleOption);
        secondParser.parse(arguments);
        handleCommandLine(secondParser);
    });

    return app.exec();
}
