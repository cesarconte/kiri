/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPainterPath>
#include <QQuickWindow>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QTimer>
#include <KDBusService>
#include <KNotification>
#include <KLocalizedString>
#include <KLocalizedContext>
#include <KWindowEffects>

#include "kityclient.h"

// Handles Double-Press Meta (⌘) both via KWin ModifierOnlyShortcuts D-Bus and in-window KeyEvents
class MetaDoubleTapHandler : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.cesar.kity.App")

public:
    explicit MetaDoubleTapHandler(QQuickWindow *window, QObject *parent = nullptr)
        : QObject(parent)
        , m_window(window)
    {
        m_singleTapTimer.setSingleShot(true);
        m_singleTapTimer.setInterval(330);
        connect(&m_singleTapTimer, &QTimer::timeout, this, []() {
            // Forward single Meta tap to Plasma Application Launcher
            QDBusMessage msg = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.plasmashell"),
                QStringLiteral("/PlasmaShell"),
                QStringLiteral("org.kde.PlasmaShell"),
                QStringLiteral("activateLauncherMenu"));
            QDBusConnection::sessionBus().asyncCall(msg);
        });
    }

public Q_SLOTS:
    Q_SCRIPTABLE void handleMetaTap()
    {
        if (m_singleTapTimer.isActive()) {
            m_singleTapTimer.stop();
            togglePill();
        } else {
            m_singleTapTimer.start();
        }
    }

    Q_SCRIPTABLE void togglePill()
    {
        if (!m_window) {
            return;
        }
        const bool isCompact = m_window->property("compactMode").toBool();
        if (m_window->isVisible() && m_window->isActive() && isCompact) {
            m_window->hide();
        } else {
            m_window->setProperty("compactMode", true);
            m_window->show();
            m_window->raise();
            m_window->requestActivate();
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if ((ke->key() == Qt::Key_Meta || ke->key() == Qt::Key_Super_L || ke->key() == Qt::Key_Super_R) && !ke->isAutoRepeat()) {
                m_onlyMetaPressed = true;
            } else {
                m_onlyMetaPressed = false;
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if ((ke->key() == Qt::Key_Meta || ke->key() == Qt::Key_Super_L || ke->key() == Qt::Key_Super_R) && !ke->isAutoRepeat() && m_onlyMetaPressed) {
                m_onlyMetaPressed = false;
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - m_lastMetaReleaseMs <= 350) {
                    m_lastMetaReleaseMs = 0;
                    togglePill();
                    return true;
                }
                m_lastMetaReleaseMs = now;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QQuickWindow *m_window = nullptr;
    QTimer m_singleTapTimer;
    bool m_onlyMetaPressed = false;
    qint64 m_lastMetaReleaseMs = 0;
};

int main(int argc, char *argv[])
{
    QQuickWindow::setDefaultAlphaBuffer(true);
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

    QCommandLineOption askScreenOption(QStringList() << QStringLiteral("s") << QStringLiteral("ask-screen"),
                                       i18n("Analizar la ventana activa y preguntar a Kity sobre ella"));
    parser.addOption(askScreenOption);

    QCommandLineOption newChatOption(QStringList() << QStringLiteral("n") << QStringLiteral("new-chat"),
                                     i18n("Iniciar una nueva conversación limpia"));
    parser.addOption(newChatOption);

    QCommandLineOption toggleOption(QStringList() << QStringLiteral("t") << QStringLiteral("toggle"),
                                    i18n("Alternar visibilidad de la ventana de Kity"));
    parser.addOption(toggleOption);

    QCommandLineOption compactOption(QStringList() << QStringLiteral("compact"),
                                     i18n("Abrir en modo píldora flotante compacta (Siri Island)"));
    parser.addOption(compactOption);

    QCommandLineOption fileOption(QStringList() << QStringLiteral("f") << QStringLiteral("file"),
                                  i18n("Analizar o preguntar sobre un archivo seleccionado"),
                                  QStringLiteral("ruta"));
    parser.addOption(fileOption);

    QCommandLineOption summarizeOption(QStringList() << QStringLiteral("summarize"),
                                       i18n("Generar un resumen estructurado del archivo indicado"),
                                       QStringLiteral("ruta"));
    parser.addOption(summarizeOption);

    QCommandLineOption addKnowledgeOption(QStringList() << QStringLiteral("add-knowledge"),
                                          i18n("Añadir un archivo a la base de conocimiento local (RAG)"),
                                          QStringLiteral("ruta"));
    parser.addOption(addKnowledgeOption);

    QCommandLineOption writingToolsOption(QStringList() << QStringLiteral("w") << QStringLiteral("writing-tools"),
                                          i18n("Abrir el panel de Herramientas de Escritura de Kity (Apple Intelligence)"));
    parser.addOption(writingToolsOption);

    QCommandLineOption writingActionOption(QStringList() << QStringLiteral("writing-action"),
                                           i18n("Acción directa de escritura (proofread, rewrite, friendly, professional, concise, summary, keypoints, table, translate:<lang>)"),
                                           QStringLiteral("accion"));
    parser.addOption(writingActionOption);

    parser.addPositionalArgument(QStringLiteral("input"), i18n("Pregunta de texto o ruta de archivo opcional"));

    parser.process(app);

    KDBusService service(KDBusService::Unique);

    KityClient client;
    QQmlApplicationEngine engine;
    engine.addImportPath(QDir::homePath() + QStringLiteral("/.local/lib/qt6/qml"));
    engine.addImportPath(QStringLiteral("/usr/lib/qt6/qml"));
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));

    engine.rootContext()->setContextProperty(QStringLiteral("kityClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("Plasmoid"), &client);

    engine.load(QUrl(QStringLiteral("qrc:/qml/app/qml/MainWindow.qml")));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    QObject *rootObj = engine.rootObjects().first();
    QQuickWindow *window = qobject_cast<QQuickWindow *>(rootObj);
    auto *metaHandler = new MetaDoubleTapHandler(window, &app);
    app.installEventFilter(metaHandler);
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.cesar.kity.App"));
    QDBusConnection::sessionBus().registerObject(
        QStringLiteral("/KityApp"),
        metaHandler,
        QDBusConnection::ExportAllSlots);

    if (window) {
        auto updateBlurRegion = [window]() {
            const bool compact = window->property("compactMode").toBool();
            const qreal radius = compact ? 24.0 : 20.0;
            QPainterPath path;
            path.addRoundedRect(QRectF(0, 0, window->width(), window->height()), radius, radius);
            KWindowEffects::enableBlurBehind(window, true, QRegion(path.toFillPolygon().toPolygon()));
        };
        updateBlurRegion();
        QObject::connect(window, &QWindow::widthChanged, window, updateBlurRegion);
        QObject::connect(window, &QWindow::heightChanged, window, updateBlurRegion);
    }

    auto handleCommandLine = [&](const QCommandLineParser &p) {
        if (p.isSet(compactOption) && window) {
            window->setProperty("compactMode", true);
        }
        if (p.isSet(newChatOption)) {
            client.clearHistory();
        }
        if (p.isSet(listenOption)) {
            client.startRecording();
        }
        if (p.isSet(ocrOption)) {
            client.runVisualIntelligence(QStringLiteral("region"), QStringLiteral("ocr"));
        }
        if (p.isSet(askScreenOption)) {
            client.runVisualIntelligence(QStringLiteral("activewindow"), QStringLiteral("explain"));
        }

        auto processFile = [&](const QString &path, bool summarizeOnly) {
            if (path.isEmpty()) return;
            QFileInfo fi(path);
            if (!fi.exists()) return;

            QString extractedContent;
            const QString ext = fi.suffix().toLower();

            if (ext == QLatin1String("pdf")) {
                QProcess pPdf;
                pPdf.start(QStringLiteral("pdftotext"), {path, QStringLiteral("-")});
                pPdf.waitForFinished(4000);
                extractedContent = QString::fromUtf8(pPdf.readAllStandardOutput()).trimmed();
            } else if (ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") || ext == QLatin1String("webp")) {
                QProcess pOcr;
                pOcr.start(QStringLiteral("tesseract"), {path, QStringLiteral("stdout"), QStringLiteral("--psm"), QStringLiteral("3")});
                pOcr.waitForFinished(4000);
                extractedContent = QString::fromUtf8(pOcr.readAllStandardOutput()).trimmed();
            } else {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    extractedContent = QString::fromUtf8(f.readAll()).trimmed();
                }
            }

            if (extractedContent.isEmpty()) {
                client.sendMessage(i18n("No se pudo extraer texto legible del archivo %1.", fi.fileName()));
                return;
            }

            if (extractedContent.length() > 14000) {
                extractedContent = extractedContent.left(14000) + QStringLiteral("\n\n[...Texto truncado para el contexto del modelo...]");
            }

            QString prompt;
            if (summarizeOnly) {
                prompt = QStringLiteral("Por favor, resume de forma clara, ejecutiva y estructurada el siguiente documento (%1):\n\n\"\"\"\n%2\n\"\"\"")
                            .arg(fi.fileName(), extractedContent);
            } else {
                prompt = QStringLiteral("He abierto el archivo \"%1\". Este es su contenido principal:\n\n\"\"\"\n%2\n\"\"\"\n\nExplica brevemente de qué trata este documento y qué puntos clave contiene.")
                            .arg(fi.fileName(), extractedContent);
            }
            client.sendMessage(prompt);
        };

        if (p.isSet(fileOption)) {
            processFile(p.value(fileOption), false);
        } else if (p.isSet(summarizeOption)) {
            processFile(p.value(summarizeOption), true);
        } else if (p.isSet(addKnowledgeOption)) {
            const QString srcPath = p.value(addKnowledgeOption);
            QFileInfo fi(srcPath);
            if (fi.exists()) {
                const QString kbDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
                QDir().mkpath(kbDir);
                const QString destPath = kbDir + QStringLiteral("/") + fi.fileName();
                QFile::remove(destPath);
                QFile::copy(srcPath, destPath);
                KNotification::event(QStringLiteral("notification"),
                                     i18n("Base de Conocimiento de Kity"),
                                     i18n("Se ha añadido «%1» a tu base de conocimiento local para consultas semánticas.", fi.fileName()),
                                     QStringLiteral("org.cesar.kity"));
            }
        } else if (p.isSet(writingToolsOption) || p.isSet(writingActionOption)) {
            QString action;
            if (p.isSet(writingActionOption)) {
                action = p.value(writingActionOption).trimmed();
            } else if (!p.positionalArguments().isEmpty()) {
                const QString firstArg = p.positionalArguments().first().trimmed();
                static const QStringList validActions = {
                    QStringLiteral("proofread"), QStringLiteral("rewrite"), QStringLiteral("friendly"),
                    QStringLiteral("professional"), QStringLiteral("concise"), QStringLiteral("summary"),
                    QStringLiteral("keypoints"), QStringLiteral("table")
                };
                if (validActions.contains(firstArg) || firstArg.startsWith(QStringLiteral("translate:"))) {
                    action = firstArg;
                }
            }

            if (!action.isEmpty()) {
                client.applyWritingTool(action);
            } else if (window) {
                window->setProperty("showWritingTools", true);
            }
        } else if (!p.positionalArguments().isEmpty()) {
            const QString pos = p.positionalArguments().first();
            QFileInfo fi(pos);
            if (fi.exists() && fi.isFile()) {
                processFile(pos, false);
            } else if (!pos.trimmed().isEmpty()) {
                client.sendMessage(pos.trimmed());
            }
        }

        if (window) {
            const auto bringToFront = [window]() {
                if (window->visibility() == QWindow::Minimized) {
                    window->showNormal();
                } else {
                    window->show();
                }
                window->raise();
                window->requestActivate();
            };

            // Toggle only hides when Kity is visible AND focused. If it is
            // visible but behind other windows (or on another desktop), the
            // user expects it to come to the front, not to disappear.
            if (p.isSet(toggleOption) && window->isVisible() && window->isActive()
                && window->visibility() != QWindow::Minimized) {
                window->hide();
            } else {
                bringToFront();
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
        secondParser.addOption(askScreenOption);
        secondParser.addOption(newChatOption);
        secondParser.addOption(toggleOption);
        secondParser.addOption(compactOption);
        secondParser.addOption(fileOption);
        secondParser.addOption(summarizeOption);
        secondParser.addOption(addKnowledgeOption);
        secondParser.addOption(writingToolsOption);
        secondParser.addOption(writingActionOption);
        secondParser.addPositionalArgument(QStringLiteral("input"), i18n("Pregunta de texto o ruta de archivo opcional"));
        secondParser.parse(arguments);
        handleCommandLine(secondParser);
    });

    return app.exec();
}

#include "main.moc"
