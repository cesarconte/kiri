/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>

#include "system/applicationmanager.h"
#include "system/windowmanager.h"
#include "system/audiomanager.h"
#include "system/mediamanager.h"
#include "system/devicemanager.h"
#include "system/timermanager.h"
#include "system/weathermanager.h"
#include "system/hardwaremanager.h"
#include "system/clipboardmanager.h"
#include "system/terminalmanager.h"
#include "system/websearchmanager.h"
#include "system/calendarmanager.h"
#include "system/emailmanager.h"
#include "tools/toolregistry.h"
#include "tools/launchapptool.h"
#include "tools/closeapptool.h"
#include "tools/mediacontroltool.h"
#include "tools/timertool.h"
#include "tools/weathertool.h"
#include "tools/systemvitalstool.h"
#include "tools/clipboardtool.h"
#include "tools/terminalcommandtool.h"
#include "tools/websearchtool.h"
#include "tools/calendareventtool.h"
#include "tools/emailtool.h"
#include "system/notificationmanager.h"
#include "tools/notificationtool.h"
#include "ai/intentparser.h"
#include "ai/knowledgemanager.h"
#include "ai/visionengine.h"

class TestAppManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        Kity::ApplicationManager::instance().reloadCatalog();
    }

    void testSearchGoogleMaps()
    {
        const auto matches = Kity::ApplicationManager::instance().searchApps(QStringLiteral("Google Maps"));
        QVERIFY(!matches.isEmpty());
        bool found = false;
        for (const auto &m : matches) {
            if (m.name.contains(QStringLiteral("Google Maps"), Qt::CaseInsensitive)
                || m.desktopEntryName.contains(QStringLiteral("google-maps"), Qt::CaseInsensitive)) {
                found = true;
                break;
            }
        }
        QVERIFY2(found, "Google Maps desktop service should be indexed in application catalog");
    }

    void testToolRegistryContainsLaunchAndClose()
    {
        auto launchTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("launch_application"));
        QVERIFY(launchTool != nullptr);
        QCOMPARE(launchTool->name(), QStringLiteral("launch_application"));

        auto closeTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("close_application"));
        QVERIFY(closeTool != nullptr);
        QCOMPARE(closeTool->name(), QStringLiteral("close_application"));

        auto mediaTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("media_control"));
        QVERIFY(mediaTool != nullptr);
        QCOMPARE(mediaTool->name(), QStringLiteral("media_control"));

        const QJsonObject launchParams = launchTool->parameters();
        QVERIFY(launchParams.contains(QStringLiteral("properties")));
        QVERIFY(launchParams.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("app_name")));

        const QJsonObject closeParams = closeTool->parameters();
        QVERIFY(closeParams.contains(QStringLiteral("properties")));
        QVERIFY(closeParams.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("app_name")));

        const QJsonObject mediaParams = mediaTool->parameters();
        QVERIFY(mediaParams.contains(QStringLiteral("properties")));
        QVERIFY(mediaParams.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("action")));
    }

    void testToolDefinitionsGeneration()
    {
        const QJsonArray defs = Kity::ToolRegistry::instance().getToolDefinitions();
        QVERIFY(!defs.isEmpty());

        bool hasLaunch = false;
        bool hasClose = false;
        for (const auto &val : defs) {
            const QJsonObject obj = val.toObject().value(QStringLiteral("function")).toObject();
            if (obj.value(QStringLiteral("name")).toString() == QStringLiteral("launch_application")) hasLaunch = true;
            if (obj.value(QStringLiteral("name")).toString() == QStringLiteral("close_application")) hasClose = true;
        }

        QVERIFY(hasLaunch);
        QVERIFY(hasClose);
    }

    void testMediaControlStatusExecution()
    {
        auto mediaTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("media_control"));
        QVERIFY(mediaTool != nullptr);

        QJsonObject args;
        args[QStringLiteral("action")] = QStringLiteral("status");
        const QJsonObject res = mediaTool->execute(args);
        QVERIFY(res.contains(QStringLiteral("status")));
    }

    void testDeviceManagerPowerProfiles()
    {
        const QString desc = Kity::DeviceManager::instance().getActivePowerProfileDescription();
        QVERIFY(!desc.isEmpty());
    }

    void testAudioManagerStatus()
    {
        const QString volStatus = Kity::AudioManager::instance().getAudioVolumeStatus();
        QVERIFY(!volStatus.isEmpty());
    }

    void testTimerManagerAndTool()
    {
        auto timerTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("set_timer"));
        QVERIFY(timerTool != nullptr);

        QJsonObject args;
        args[QStringLiteral("seconds")] = 300;
        args[QStringLiteral("label")] = QStringLiteral("Test Pasta");

        const QJsonObject res = timerTool->execute(args);
        QVERIFY(res.value(QStringLiteral("success")).toBool());
        QCOMPARE(res.value(QStringLiteral("seconds")).toInt(), 300);
        QCOMPARE(res.value(QStringLiteral("label")).toString(), QStringLiteral("Test Pasta"));

        const auto active = Kity::TimerManager::instance().listActiveReminders();
        bool found = false;
        for (const auto &r : active) {
            if (r.text == QStringLiteral("Test Pasta")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    void testWeatherTool()
    {
        auto weatherTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("get_weather"));
        QVERIFY(weatherTool != nullptr);
        QCOMPARE(weatherTool->name(), QStringLiteral("get_weather"));

        const QString defLoc = Kity::WeatherManager::instance().resolveDefaultLocation();
        QVERIFY(!defLoc.isEmpty());

        const QString condition = Kity::WeatherManager::wmoCodeToSpanish(0);
        QCOMPARE(condition, QStringLiteral("despejado"));
    }

    void testHardwareManagerAndSystemVitalsTool()
    {
        auto vitalsTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("get_system_vitals"));
        QVERIFY(vitalsTool != nullptr);
        QCOMPARE(vitalsTool->name(), QStringLiteral("get_system_vitals"));

        const auto ram = Kity::HardwareManager::instance().getRamInfo();
        QVERIFY(ram.totalGiB > 0.0);
        QVERIFY(ram.availGiB >= 0.0);

        const auto disk = Kity::HardwareManager::instance().getDiskInfo();
        QVERIFY(disk.totalGiB > 0.0);

        const QString vitals = Kity::HardwareManager::instance().collectSystemVitals();
        QVERIFY(!vitals.isEmpty());
        QVERIFY(vitals.contains(QStringLiteral("SO:")));

        const QString ramSummary = Kity::HardwareManager::instance().getSystemResourceSummary(QStringLiteral("ram"));
        QVERIFY(!ramSummary.isEmpty());
        QVERIFY(ramSummary.contains(QStringLiteral("RAM")));

        const QJsonObject res = vitalsTool->execute(QJsonObject());
        QVERIFY(res.value(QStringLiteral("success")).toBool());
        QVERIFY(!res.value(QStringLiteral("vitals")).toString().isEmpty());
        QVERIFY(res.contains(QStringLiteral("vitals_data")));
    }

    void testClipboardManagerAndTool()
    {
        auto clipTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("clipboard_tool"));
        QVERIFY(clipTool != nullptr);
        QCOMPARE(clipTool->name(), QStringLiteral("clipboard_tool"));

        const QJsonObject params = clipTool->parameters();
        QVERIFY(params.contains(QStringLiteral("properties")));
        QVERIFY(params.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("action")));

        // Test copy to clipboard
        const QString testStr = QStringLiteral("Kity Unit Test Clipboard String 12345");
        Kity::ClipboardManager::instance().copyToClipboard(testStr);

        // Test get_current
        QJsonObject args;
        args[QStringLiteral("action")] = QStringLiteral("get_current");
        const QJsonObject resCurrent = clipTool->execute(args);
        QVERIFY(resCurrent.value(QStringLiteral("success")).toBool());
        QVERIFY(resCurrent.contains(QStringLiteral("clipboard_content")));

        // Test list_history
        args[QStringLiteral("action")] = QStringLiteral("list_history");
        const QJsonObject resHistory = clipTool->execute(args);
        QVERIFY(resHistory.value(QStringLiteral("success")).toBool());
        QVERIFY(resHistory.contains(QStringLiteral("history")));

        // Test search
        args[QStringLiteral("action")] = QStringLiteral("search");
        args[QStringLiteral("query")] = QStringLiteral("Test");
        const QJsonObject resSearch = clipTool->execute(args);
        QVERIFY(resSearch.value(QStringLiteral("success")).toBool());
        QVERIFY(resSearch.contains(QStringLiteral("matches")));
    }

    void testTerminalManagerAndTool()
    {
        auto termTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("run_terminal_command"));
        QVERIFY(termTool != nullptr);
        QCOMPARE(termTool->name(), QStringLiteral("run_terminal_command"));

        const QJsonObject params = termTool->parameters();
        QVERIFY(params.contains(QStringLiteral("properties")));
        QVERIFY(params.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("command")));

        // Test sanitizeCommand (Arch/CachyOS strict requirement: cachy-update preferred)
        const QString sanitizedApt = Kity::TerminalManager::instance().sanitizeCommand(QStringLiteral("sudo apt update && sudo apt upgrade"));
        QCOMPARE(sanitizedApt, QStringLiteral("cachy-update"));

        const QString normalCmd = Kity::TerminalManager::instance().sanitizeCommand(QStringLiteral("fastfetch"));
        QCOMPARE(normalCmd, QStringLiteral("fastfetch"));

        // Test isUpdateCommand
        QVERIFY(Kity::TerminalManager::instance().isUpdateCommand(QStringLiteral("cachy-update")));
        QVERIFY(Kity::TerminalManager::instance().isUpdateCommand(QStringLiteral("arch-update")));
        QVERIFY(Kity::TerminalManager::instance().isUpdateCommand(QStringLiteral("sudo pacman -Syu")));
        QVERIFY(Kity::TerminalManager::instance().isUpdateCommand(QStringLiteral("paru -Syu")));
        QVERIFY(Kity::TerminalManager::instance().isUpdateCommand(QStringLiteral("flatpak update")));
        QVERIFY(!Kity::TerminalManager::instance().isUpdateCommand(QStringLiteral("fastfetch")));

        // Test extractTerminalCommands from markdown
        const QString md = QStringLiteral("Aquí tienes el comando para ver el sistema:\n```bash\nfastfetch\n```\nY para listar:\n```sh\nls -la\n```");
        const QStringList extracted = Kity::TerminalManager::instance().extractTerminalCommands(md);
        QCOMPARE(extracted.size(), 2);
        QCOMPARE(extracted.at(0), QStringLiteral("fastfetch"));
        QCOMPARE(extracted.at(1), QStringLiteral("ls -la"));
    }

    void testWebSearchManagerAndTool()
    {
        auto searchTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("web_search"));
        QVERIFY(searchTool != nullptr);
        QCOMPARE(searchTool->name(), QStringLiteral("web_search"));

        const QJsonObject params = searchTool->parameters();
        QVERIFY(params.contains(QStringLiteral("properties")));
        QVERIFY(params.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("query")));

        // Test HTML article cleaning
        const QString sampleHtml = QStringLiteral(
            "<html><head><script>alert('test');</script><style>.cls{color:red;}</style></head>"
            "<body><nav>Menu link 1</nav>"
            "<article><h1>Título del artículo importante</h1>"
            "<p>Este es un párrafo de texto sustantivo con información relevante y detallada sobre Linux y CachyOS.</p>"
            "<p>Short</p>"
            "<p>Otro párrafo largo y sustantivo para verificar que la extracción de texto funciona de manera impecable.</p>"
            "</article>"
            "<footer>Pie de página con copyright</footer></body></html>"
        );

        const QString cleaned = Kity::WebSearchManager::instance().extractCleanArticleText(sampleHtml);
        QVERIFY(!cleaned.isEmpty());
        QVERIFY(!cleaned.contains(QStringLiteral("<script>")));
        QVERIFY(!cleaned.contains(QStringLiteral("alert")));
        QVERIFY(!cleaned.contains(QStringLiteral("Menu link")));
        QVERIFY(cleaned.contains(QStringLiteral("información relevante")));
        QVERIFY(!cleaned.contains(QStringLiteral("Short")));
    }

    void testCalendarManagerAndTool()
    {
        auto calTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("calendar_event"));
        QVERIFY(calTool != nullptr);
        QCOMPARE(calTool->name(), QStringLiteral("calendar_event"));

        const QJsonObject params = calTool->parameters();
        QVERIFY(params.contains(QStringLiteral("properties")));
        QVERIFY(params.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("action")));

        // Test prompt parsing
        const auto ev = Kity::CalendarManager::instance().parsePrompt(QStringLiteral("Reunión con el equipo a las 16:30 mañana"));
        QCOMPARE(ev.startTime.time().hour(), 16);
        QCOMPARE(ev.startTime.time().minute(), 30);
        QCOMPARE(ev.calendarName, QStringLiteral("César Conte"));

        // Test ICS generation
        const QString icsPath = Kity::CalendarManager::instance().generateIcsFile(ev);
        QVERIFY(!icsPath.isEmpty());
        QVERIFY(QFile::exists(icsPath));

        QFile f(icsPath);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString icsContent = QString::fromUtf8(f.readAll());
        f.close();

        QVERIFY(icsContent.contains(QStringLiteral("BEGIN:VCALENDAR")));
        QVERIFY(icsContent.contains(QStringLiteral("X-WR-CALNAME:César Conte")));
        QVERIFY(icsContent.contains(QStringLiteral("BEGIN:VEVENT")));
    }

    void testEmailManagerAndTool()
    {
        auto mailTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("email_tool"));
        QVERIFY(mailTool != nullptr);
        QCOMPARE(mailTool->name(), QStringLiteral("email_tool"));

        // Verify Proton Mail is installed on the user's CachyOS system
        QVERIFY(Kity::EmailManager::instance().isProtonMailInstalled());

        // Test mailto URL building
        const QUrl url = Kity::EmailManager::instance().buildMailtoUrl(QStringLiteral("test@proton.me"), QStringLiteral("Asunto Kity"), QStringLiteral("Mensaje de prueba"));
        QCOMPARE(url.scheme(), QStringLiteral("mailto"));
        QCOMPARE(url.path(), QStringLiteral("test@proton.me"));
        QVERIFY(url.query().contains(QStringLiteral("subject=Asunto")));
        QVERIFY(url.query().contains(QStringLiteral("body=Mensaje")));

        // Test Contact resolution
        QString resEmail, resName;
        QVERIFY(Kity::EmailManager::instance().resolveContact(QStringLiteral("contacto@empresa.com"), &resEmail, &resName));
        QCOMPARE(resEmail, QStringLiteral("contacto@empresa.com"));

        // Test Draft Preparation with Tone Refinement (Safety Guardrail)
        const auto draft = Kity::EmailManager::instance().prepareDraft(
            QStringLiteral("cliente@ejemplo.com"),
            QStringLiteral("Presupuesto"),
            QStringLiteral("Adjunto propuesta comercial."),
            QStringLiteral("proton"),
            QStringLiteral("formal")
        );
        QCOMPARE(draft.status, QStringLiteral("awaiting_confirmation"));
        QVERIFY(draft.body.contains(QStringLiteral("Estimado")));
        QVERIFY(draft.body.contains(QStringLiteral("Atentamente")));
        QVERIFY(Kity::EmailManager::instance().hasPendingDraft());

        // Test Card Map generation for CachyOS.UI EmailCard
        const QVariantMap cardMap = draft.toCardMap();
        QCOMPARE(cardMap.value(QStringLiteral("to")).toString(), QStringLiteral("cliente@ejemplo.com"));
        QCOMPARE(cardMap.value(QStringLiteral("status")).toString(), QStringLiteral("awaiting_confirmation"));

        // Test Semantic Prompt Parsing
        const auto parsed1 = Kity::EmailManager::instance().parsePrompt(
            QStringLiteral("Redacta un email a test@test.com con el siguiente texto: \"Probando las nuevas implementaciones en Kity\"")
        );
        QCOMPARE(parsed1.to, QStringLiteral("test@test.com"));
        QCOMPARE(parsed1.body, QStringLiteral("Probando las nuevas implementaciones en Kity"));

        const auto parsed2 = Kity::EmailManager::instance().parsePrompt(
            QStringLiteral("Envía un correo a juan@empresa.com con asunto Reunión y cuerpo Nos vemos a las cinco en la oficina")
        );
        QCOMPARE(parsed2.to, QStringLiteral("juan@empresa.com"));
        QCOMPARE(parsed2.subject, QStringLiteral("Reunión"));
        QCOMPARE(parsed2.body, QStringLiteral("Nos vemos a las cinco en la oficina"));

        // Test Discard action
        QVERIFY(Kity::EmailManager::instance().discardPendingDraft());
        QVERIFY(!Kity::EmailManager::instance().hasPendingDraft());
    }

    void testNotificationManagerAndTool()
    {
        // Test NotificationManager queries
        const auto items = Kity::NotificationManager::instance().recentNotifications(2);
        // We verify that recentNotifications executes journalctl without crashing
        // (items may or may not be empty depending on system log level)
        for (const auto &item : items) {
            QVERIFY(!item.sender.isEmpty());
            QVERIFY(item.urgency == QStringLiteral("normal") || item.urgency == QStringLiteral("critical") || item.urgency == QStringLiteral("low"));
            const QJsonObject json = item.toJson();
            QVERIFY(json.contains(QStringLiteral("sender")));
            QVERIFY(json.contains(QStringLiteral("summary")));
            QVERIFY(json.contains(QStringLiteral("body")));
        }

        const QString context = Kity::NotificationManager::instance().collectRecentNotificationsContext(2);
        QVERIFY(!context.isEmpty());

        // Test NotificationTool in ToolRegistry
        auto notifTool = Kity::ToolRegistry::instance().findTool(QStringLiteral("summarize_notifications"));
        QVERIFY(notifTool != nullptr);
        QCOMPARE(notifTool->name(), QStringLiteral("summarize_notifications"));

        const QJsonObject params = notifTool->parameters();
        QVERIFY(params.contains(QStringLiteral("properties")));

        // Test tool execution
        QJsonObject args;
        args[QStringLiteral("hours")] = 2;
        const QJsonObject result = Kity::ToolRegistry::instance().executeTool(QStringLiteral("summarize_notifications"), args);
        QVERIFY(result.value(QStringLiteral("success")).toBool());
        QVERIFY(result.contains(QStringLiteral("notifications_summary")));
        QVERIFY(result.contains(QStringLiteral("items")));
        QCOMPARE(result.value(QStringLiteral("count")).toInt(), items.size());
    }

    void testIntentParser()
    {
        // 1. Time query
        const auto timeRes = Kity::IntentParser::instance().execute(QStringLiteral("¿Qué hora es?"));
        QVERIFY(timeRes.handled);
        QVERIFY(timeRes.feedback.contains(QStringLiteral("Son las")));

        // 2. Date query
        const auto dateRes = Kity::IntentParser::instance().execute(QStringLiteral("Dime la fecha de hoy"));
        QVERIFY(dateRes.handled);
        QVERIFY(dateRes.feedback.contains(QStringLiteral("Hoy es")));

        // 3. Unknown intent
        const auto unknownRes = Kity::IntentParser::instance().execute(QStringLiteral("Escribe un poema sobre las mariposas azules"));
        QVERIFY(!unknownRes.handled);
    }

    void testKnowledgeManager()
    {
        Kity::KnowledgeManager::instance().ensureKnowledgeBaseReady();
        const int docCount = Kity::KnowledgeManager::instance().knowledgeDocumentCount();
        QVERIFY(docCount >= 0);
        QVERIFY(!Kity::KnowledgeManager::instance().knowledgeDirectoryPath().isEmpty());
    }

    void testVisionEngine()
    {
        const QString sampleOcr = QStringLiteral("Visita https://cachyos.org o escribe a info@cachyos.org para soporte +34985123456.");
        const QVariantMap entities = Kity::VisionEngine::parseLiveTextEntities(sampleOcr);
        QVERIFY(entities.contains(QStringLiteral("urls")));
        QVERIFY(entities.contains(QStringLiteral("emails")));
        QVERIFY(entities.contains(QStringLiteral("phones")));

        const QStringList urls = entities.value(QStringLiteral("urls")).toStringList();
        QVERIFY(urls.contains(QStringLiteral("https://cachyos.org")));

        const QStringList emails = entities.value(QStringLiteral("emails")).toStringList();
        QVERIFY(emails.contains(QStringLiteral("info@cachyos.org")));
    }

    void cleanupTestCase()
    {
        QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-f"), QStringLiteral("wl-copy.*Kity Unit Test")});
    }
};

QTEST_MAIN(TestAppManager)
#include "test_appmanager.moc"
