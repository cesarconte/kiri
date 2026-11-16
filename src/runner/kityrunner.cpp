/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kityrunner.h"

#include <KLocalizedString>
#include <KPluginFactory>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

K_PLUGIN_CLASS_WITH_JSON(KityRunner, "krunner_kity.json")

KityRunner::KityRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
{
}

void KityRunner::init()
{
    setMinLetterCount(2);
    addSyntax(QStringList{QStringLiteral("? :q:"), QStringLiteral("kity :q:"), QStringLiteral("ia :q:")},
              i18n("Pregunta directamente a Kity, ejecuta acciones del sistema o busca en tu base de conocimiento local (RAG)"));
    addSyntax(QStringLiteral("escribe :q:"),
              i18n("Aplica las Herramientas de Escritura de Kity sobre el portapapeles o texto indicado"));
    addSyntax(QStringList{QStringLiteral("traduce :q:"), QStringLiteral("translate :q:")},
              i18n("Traduce el texto indicado (o tu selección / portapapeles) con Kity"));
    addSyntax(QStringLiteral("ocr"),
              i18n("Extrae texto de una región de la pantalla (Live Text con Spectacle Wayland)"));
}

QList<KityRunner::KnowledgeHit> KityRunner::searchKnowledgeBase(const QString &query) const
{
    QList<KnowledgeHit> hits;
    const QString kbDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kity/knowledge");
    QDir dir(kbDir);
    if (!dir.exists()) {
        return hits;
    }

    const QStringList tokens = query.toLower().split(QRegularExpression(QStringLiteral("[\\s,;:.!?¡¿]+")), Qt::SkipEmptyParts);
    QStringList keywords;
    for (const QString &t : tokens) {
        if (t.length() >= 3 && t != QStringLiteral("kity") && t != QStringLiteral("nota") && t != QStringLiteral("notas")) {
            keywords.append(t);
        }
    }
    if (keywords.isEmpty()) {
        return hits;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.md"), QDir::Files);
    for (const QString &fname : files) {
        if (fname.startsWith(QStringLiteral("README"), Qt::CaseInsensitive)) {
            continue;
        }
        QFile f(dir.filePath(fname));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll());
            int score = 0;
            for (const QString &kw : keywords) {
                if (fname.contains(kw, Qt::CaseInsensitive)) score += 3;
                if (content.contains(kw, Qt::CaseInsensitive)) score += 2;
            }
            if (score > 0) {
                KnowledgeHit hit;
                hit.filePath = dir.filePath(fname);
                hit.fileName = fname;
                hit.preview = content.simplified().left(110);
                hit.relevance = qMin(0.95, 0.55 + score * 0.08);
                hits.append(hit);
            }
        }
    }
    return hits;
}

void KityRunner::match(KRunner::RunnerContext &context)
{
    const QString rawQuery = context.query().trimmed();
    if (rawQuery.length() < 2) {
        return;
    }

    // 0. Root Kity activation ("kity" or "ia" alone)
    if (rawQuery.compare(QStringLiteral("kity"), Qt::CaseInsensitive) == 0
        || rawQuery.compare(QStringLiteral("ia"), Qt::CaseInsensitive) == 0) {
        KRunner::QueryMatch m1(this);
        m1.setId(QStringLiteral("kity_open_pill"));
        m1.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::Highest);
        m1.setRelevance(1.0);
        m1.setIconName(QStringLiteral("org.cesar.kity"));
        m1.setText(i18n("Kity: Abrir Cápsula Rápida (Search or Ask)"));
        m1.setSubtext(i18n("Asistente de IA estilo macOS Golden Gate para KDE Plasma 6 (Meta + K)"));
        QVariantMap p1;
        p1[QStringLiteral("type")] = QStringLiteral("open_pill");
        m1.setData(p1);
        context.addMatch(m1);

        KRunner::QueryMatch m2(this);
        m2.setId(QStringLiteral("kity_open_listen"));
        m2.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::High);
        m2.setRelevance(0.92);
        m2.setIconName(QStringLiteral("audio-input-microphone"));
        m2.setText(i18n("Kity: Hablar por voz"));
        m2.setSubtext(i18n("Activar el micrófono y dictar una consulta en lenguaje natural"));
        QVariantMap p2;
        p2[QStringLiteral("type")] = QStringLiteral("listen");
        m2.setData(p2);
        context.addMatch(m2);

        KRunner::QueryMatch m3(this);
        m3.setId(QStringLiteral("kity_open_wt"));
        m3.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::Moderate);
        m3.setRelevance(0.85);
        m3.setIconName(QStringLiteral("document-edit-symbolic"));
        m3.setText(i18n("Kity: Herramientas de Escritura (Apple Intelligence)"));
        m3.setSubtext(i18n("Corregir, reescribir o resumir texto de tu selección o portapapeles"));
        QVariantMap p3;
        p3[QStringLiteral("type")] = QStringLiteral("writing_tools_panel");
        m3.setData(p3);
        context.addMatch(m3);

        return;
    }

    if (rawQuery.length() < 3) {
        return;
    }

    // 1. Live Text / Visual Intelligence shortcut in Spotlight ("ocr", "livetext", "captura texto")
    if (rawQuery.compare(QStringLiteral("ocr"), Qt::CaseInsensitive) == 0
        || rawQuery.compare(QStringLiteral("livetext"), Qt::CaseInsensitive) == 0
        || rawQuery.startsWith(QStringLiteral("kity ocr"), Qt::CaseInsensitive)) {
        KRunner::QueryMatch m(this);
        m.setId(QStringLiteral("kity_livetext_region"));
        m.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::Highest);
        m.setRelevance(0.98);
        m.setIconName(QStringLiteral("rectangular-snip-symbolic"));
        m.setText(i18n("Kity Live Text: Extraer texto de región en pantalla"));
        m.setSubtext(i18n("Selecciona un área con Spectacle Wayland y copia el texto extraído (OCR)"));
        QVariantMap payload;
        payload[QStringLiteral("type")] = QStringLiteral("livetext");
        payload[QStringLiteral("mode")] = QStringLiteral("region");
        m.setData(payload);
        context.addMatch(m);
        return;
    }

    // 2. Writing Tools trigger ("escribe ...", "corrige", "reescribe", "resume portapapeles")
    if (rawQuery.startsWith(QStringLiteral("escribe"), Qt::CaseInsensitive)
        || rawQuery.startsWith(QStringLiteral("corrige"), Qt::CaseInsensitive)
        || rawQuery.startsWith(QStringLiteral("reescribe"), Qt::CaseInsensitive)
        || rawQuery.startsWith(QStringLiteral("kity escribe"), Qt::CaseInsensitive)) {
        struct ToolOpt {
            QString id;
            QString title;
            QString sub;
            qreal rel;
        };
        const QList<ToolOpt> tools = {
            {QStringLiteral("proofread"), i18n("✍️ Kity: Corregir ortografía y estilo"), i18n("Aplica Proofread sobre tu selección o portapapeles de Wayland"), 0.95},
            {QStringLiteral("rewrite"), i18n("✍️ Kity: Reescribir texto"), i18n("Mejora la fluidez del texto seleccionado o en portapapeles"), 0.90},
            {QStringLiteral("professional"), i18n("✍️ Kity: Tono profesional"), i18n("Transforma el texto del portapapeles a registro ejecutivo"), 0.85},
            {QStringLiteral("summary"), i18n("✍️ Kity: Resumir texto"), i18n("Genera un resumen ejecutivo del portapapeles"), 0.82}
        };
        for (const auto &t : tools) {
            KRunner::QueryMatch m(this);
            m.setId(QStringLiteral("kity_wt_") + t.id);
            m.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::High);
            m.setRelevance(t.rel);
            m.setIconName(QStringLiteral("document-edit-symbolic"));
            m.setText(t.title);
            m.setSubtext(t.sub);
            QVariantMap payload;
            payload[QStringLiteral("type")] = QStringLiteral("writing_tool");
            payload[QStringLiteral("action")] = t.id;
            m.setData(payload);
            context.addMatch(m);
        }
        return;
    }

    // 2b. System-wide translation ("traduce [texto]", "translate [text]"): selection/clipboard when no text given
    static const QRegularExpression translateRx(QStringLiteral("^(?:kity\\s+)?(?:traduce|traducir|translate)\\b\\s*(.*)$"),
                                                QRegularExpression::CaseInsensitiveOption);
    const auto tm = translateRx.match(rawQuery);
    if (tm.hasMatch()) {
        const QString inlineText = tm.captured(1).trimmed();
        const QString what = inlineText.isEmpty() ? i18n("tu selección o portapapeles") : i18n("«%1»", inlineText.left(60));
        const QList<QPair<QString, QString>> targets = {{QStringLiteral("en"), i18n("inglés")}, {QStringLiteral("es"), i18n("español")},
                                                        {QStringLiteral("fr"), i18n("francés")}, {QStringLiteral("de"), i18n("alemán")}};
        qreal rel = 0.96;
        for (const auto &t : targets) {
            KRunner::QueryMatch m(this);
            m.setId(QStringLiteral("kity_tr_") + t.first);
            m.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::High);
            m.setRelevance(rel);
            rel -= 0.04;
            m.setIconName(QStringLiteral("translate-symbolic"));
            m.setText(i18n("🌐 Kity: Traducir al %1", t.second));
            m.setSubtext(i18n("Traduce %1 con IA local (Ollama)", what));
            QVariantMap payload;
            payload[QStringLiteral("type")] = QStringLiteral("writing_tool");
            payload[QStringLiteral("action")] = QString(QStringLiteral("translate:") + t.first);
            payload[QStringLiteral("text")] = inlineText;
            m.setData(payload);
            context.addMatch(m);
        }
        return;
    }

    // 3. Direct Kity AI / Hardware & System Intent query ("? ...", "kity ...", "ia ...", or natural question ending with "?")
    static const QRegularExpression prefixRx(
        QStringLiteral("^(?:kity\\s+|ia\\s+|\\?\\s*|¿\\s*)(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto pm = prefixRx.match(rawQuery);
    const bool hasExplicitPrefix = pm.hasMatch();
    const QString cleanPrompt = hasExplicitPrefix ? pm.captured(1).trimmed() : rawQuery;

    if (hasExplicitPrefix || rawQuery.endsWith(QLatin1Char('?')) || rawQuery.startsWith(QChar(u'¿')) || rawQuery.startsWith(QLatin1Char('?'))) {
        // Check if this is a hardware control query for custom subtext and icon
        QString hwTitle;
        QString hwSubtext;
        QString hwIcon = QStringLiteral("org.cesar.kity");

        if (cleanPrompt.contains(QRegularExpression(QStringLiteral("(?:volumen|sonido|audio|silencia)"), QRegularExpression::CaseInsensitiveOption))) {
            hwTitle = i18n("🔊 Control de Hardware: «%1»", cleanPrompt);
            hwSubtext = i18n("Ajustar audio mediante PipeWire / WirePlumber sin latencia");
            hwIcon = QStringLiteral("audio-volume-high");
        } else if (cleanPrompt.contains(QRegularExpression(QStringLiteral("brillo"), QRegularExpression::CaseInsensitiveOption))) {
            hwTitle = i18n("☀️ Control de Pantalla: «%1»", cleanPrompt);
            hwSubtext = i18n("Control de brillo vía KDE Solid PowerManagement");
            hwIcon = QStringLiteral("video-display-brightness");
        } else if (cleanPrompt.contains(QRegularExpression(QStringLiteral("(?:energ[ií]a|ahorro|rendimiento|equilibrado|bater[ií]a)"), QRegularExpression::CaseInsensitiveOption))) {
            hwTitle = i18n("🔋 Control de Energía: «%1»", cleanPrompt);
            hwSubtext = i18n("Perfil de energía mediante power-profiles-daemon");
            hwIcon = QStringLiteral("battery-profile-powersave");
        }

        KRunner::QueryMatch m(this);
        m.setId(QStringLiteral("kity_ask_") + cleanPrompt);
        m.setCategoryRelevance(hasExplicitPrefix
                               ? KRunner::QueryMatch::CategoryRelevance::Highest
                               : KRunner::QueryMatch::CategoryRelevance::Moderate);
        m.setRelevance(hasExplicitPrefix ? 0.98 : 0.72);
        m.setIconName(hwIcon);
        m.setText(!hwTitle.isEmpty() ? hwTitle : i18n("Preguntar a Kity: «%1»", cleanPrompt));
        m.setSubtext(!hwSubtext.isEmpty() ? hwSubtext : i18n("Procesar con Kity AI (Ollama local / Acciones del sistema Plasma 6)"));
        QVariantMap payload;
        payload[QStringLiteral("type")] = QStringLiteral("ask");
        payload[QStringLiteral("prompt")] = cleanPrompt;
        m.setData(payload);
        context.addMatch(m);
    }

    // 4. Local Knowledge Base (RAG) semantic file matches
    const auto kbHits = searchKnowledgeBase(cleanPrompt);
    for (const auto &hit : kbHits) {
        KRunner::QueryMatch m(this);
        m.setId(QStringLiteral("kity_kb_") + hit.fileName);
        m.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::High);
        m.setRelevance(hit.relevance);
        m.setIconName(QStringLiteral("text-markdown"));
        m.setText(i18n("📄 Nota en Kity RAG: %1", hit.fileName));
        m.setSubtext(hit.preview);
        QVariantMap payload;
        payload[QStringLiteral("type")] = QStringLiteral("kb_ask");
        payload[QStringLiteral("filePath")] = hit.filePath;
        payload[QStringLiteral("fileName")] = hit.fileName;
        payload[QStringLiteral("prompt")] = cleanPrompt;
        m.setData(payload);
        context.addMatch(m);
    }
}

void KityRunner::run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match)
{
    Q_UNUSED(context)
    const QVariantMap payload = match.data().toMap();
    const QString type = payload.value(QStringLiteral("type")).toString();

    const bool isServiceActive = QDBusConnection::sessionBus().interface()
        && QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.cesar.kity"));

    QDBusInterface genericIface(QStringLiteral("org.cesar.kity"),
                                QStringLiteral("/Kity"),
                                QString(),
                                QDBusConnection::sessionBus());

    const QString kityBin = QStandardPaths::findExecutable(QStringLiteral("kity"));

    if (type == QStringLiteral("open_pill")) {
        if (isServiceActive) {
            genericIface.asyncCall(QStringLiteral("toggleUI"));
        } else if (!kityBin.isEmpty()) {
            QProcess::startDetached(kityBin, {QStringLiteral("--pill")});
        }
    } else if (type == QStringLiteral("listen")) {
        if (isServiceActive) {
            genericIface.asyncCall(QStringLiteral("openUI"));
            genericIface.asyncCall(QStringLiteral("startRecording"));
        } else if (!kityBin.isEmpty()) {
            QProcess::startDetached(kityBin, {QStringLiteral("--listen")});
        }
    } else if (type == QStringLiteral("writing_tools_panel")) {
        if (isServiceActive) {
            genericIface.asyncCall(QStringLiteral("openUI"));
            genericIface.asyncCall(QStringLiteral("applyWritingTool"), QStringLiteral("proofread"), QString());
        } else if (!kityBin.isEmpty()) {
            QProcess::startDetached(kityBin, {QStringLiteral("--writing-tools")});
        }
    } else if (type == QStringLiteral("livetext")) {
        if (isServiceActive) {
            genericIface.asyncCall(QStringLiteral("runVisualIntelligence"),
                                   payload.value(QStringLiteral("mode")).toString(),
                                   QStringLiteral("ocr"),
                                   QString());
        } else if (!kityBin.isEmpty()) {
            QProcess::startDetached(kityBin, {QStringLiteral("--ocr")});
        }
    } else if (type == QStringLiteral("writing_tool")) {
        if (isServiceActive) {
            genericIface.asyncCall(QStringLiteral("applyWritingTool"),
                                   payload.value(QStringLiteral("action")).toString(),
                                   payload.value(QStringLiteral("text")).toString());
        } else if (!kityBin.isEmpty()) {
            QProcess::startDetached(kityBin, {QStringLiteral("--writing-action"), payload.value(QStringLiteral("action")).toString(), payload.value(QStringLiteral("text")).toString()});
        }
    } else if (type == QStringLiteral("ask")) {
        if (isServiceActive) {
            genericIface.asyncCall(QStringLiteral("openUI"));
            genericIface.asyncCall(QStringLiteral("sendMessage"),
                                   payload.value(QStringLiteral("prompt")).toString(),
                                   false);
        } else if (!kityBin.isEmpty()) {
            QProcess::startDetached(kityBin, {payload.value(QStringLiteral("prompt")).toString()});
        }
    } else if (type == QStringLiteral("kb_ask")) {
        const QString filePath = payload.value(QStringLiteral("filePath")).toString();
        if (!filePath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        }
    }
}

#include "kityrunner.moc"
