/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "mathnotes.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

QString MathNotes::stripAnsi(const QString &s)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
    QString out = s;
    out.remove(ansi);
    return out;
}

bool MathNotes::isEvaluableLine(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty() || t.startsWith(QLatin1Char('#')) || t.startsWith(QStringLiteral("//"))) {
        return false;
    }
    // Needs a digit or an operator/assignment (plain prose like "Compra del mes" must not become units,
    // but "total = precio * cantidad" must be evaluated even without digits).
    static const QRegularExpression mathy(QStringLiteral("[\\d=+*/^%]"));
    return mathy.match(t).hasMatch();
}

QString MathNotes::normalizeExpression(const QString &line)
{
    QString t = line.trimmed();
    // "15% of 240" / "15 % de 240"  ->  "15% * 240"
    static const QRegularExpression percentOf(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*%\\s*(?:of|de)\\s+"),
                                              QRegularExpression::CaseInsensitiveOption);
    t.replace(percentOf, QStringLiteral("\\1% * "));
    // Spanish decimal comma between digits -> dot (session runs with LC_ALL=C)
    static const QRegularExpression decimalComma(QStringLiteral("(?<=\\d),(?=\\d)"));
    t.replace(decimalComma, QStringLiteral("."));
    // Trailing "=" the user may type, as in Apple's Math Notes ("12*3 =")
    while (t.endsWith(QLatin1Char('=')) || t.endsWith(QLatin1Char(' '))) {
        t.chop(1);
    }
    return t;
}

QStringList MathNotes::parseQalcOutput(const QString &output, const QStringList &sentLines)
{
    // qalc echoes every input as "> <line>", then a blank line and the indented result.
    QStringList results;
    const QStringList lines = stripAnsi(output).split(QLatin1Char('\n'));
    int idx = 0;
    for (const QString &expected : sentLines) {
        const QString marker = QStringLiteral("> ") + expected;
        while (idx < lines.size() && lines.at(idx).trimmed() != marker.trimmed()) {
            ++idx;
        }
        if (idx >= lines.size()) {
            results << QString();
            continue;
        }
        ++idx;
        QString result;
        while (idx < lines.size() && !lines.at(idx).startsWith(QLatin1String("> "))) {
            const QString r = lines.at(idx).trimmed();
            if (!r.isEmpty() && result.isEmpty() && !r.startsWith(QStringLiteral("error"), Qt::CaseInsensitive)
                && !r.startsWith(QStringLiteral("warning"), Qt::CaseInsensitive)) {
                result = r;
            }
            ++idx;
        }
        results << result;
    }
    return results;
}

QStringList MathNotes::evaluateLines(const QString &text, int timeoutMs)
{
    const QStringList inputLines = text.split(QLatin1Char('\n'));
    QStringList results;
    for (int i = 0; i < inputLines.size(); ++i) {
        results << QString();
    }

    const QString qalc = QStandardPaths::findExecutable(QStringLiteral("qalc"));
    if (qalc.isEmpty()) {
        return results;
    }

    QStringList sent;
    QList<int> sentIndex;
    for (int i = 0; i < inputLines.size(); ++i) {
        if (isEvaluableLine(inputLines.at(i))) {
            const QString n = normalizeExpression(inputLines.at(i));
            if (!n.isEmpty()) {
                sent << n;
                sentIndex << i;
            }
        }
    }
    if (sent.isEmpty()) {
        return results;
    }

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(qalc, {QStringLiteral("-t")});
    if (!proc.waitForStarted(1000)) {
        return results;
    }
    proc.write(sent.join(QLatin1Char('\n')).toUtf8() + "\n");
    proc.closeWriteChannel();
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(300);
    }

    const QStringList parsed = parseQalcOutput(QString::fromUtf8(proc.readAllStandardOutput()), sent);
    for (int k = 0; k < sentIndex.size() && k < parsed.size(); ++k) {
        results[sentIndex.at(k)] = parsed.at(k);
    }
    return results;
}
