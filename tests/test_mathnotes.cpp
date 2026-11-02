/*
    SPDX-FileCopyrightText: 2026 Kity contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "mathnotes.h"

#include <QStandardPaths>
#include <QTest>

class TestMathNotes : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testEvaluableLineDetection()
    {
        QVERIFY(MathNotes::isEvaluableLine(QStringLiteral("12 * 3 + 4")));
        QVERIFY(MathNotes::isEvaluableLine(QStringLiteral("precio = 12.5")));
        QVERIFY(!MathNotes::isEvaluableLine(QStringLiteral("")));
        QVERIFY(!MathNotes::isEvaluableLine(QStringLiteral("   ")));
        QVERIFY(!MathNotes::isEvaluableLine(QStringLiteral("# Compra del mes")));
        QVERIFY(!MathNotes::isEvaluableLine(QStringLiteral("// nota")));
        QVERIFY(!MathNotes::isEvaluableLine(QStringLiteral("Compra del mes")));
        QVERIFY(MathNotes::isEvaluableLine(QStringLiteral("total = precio * cantidad")));
        QVERIFY(MathNotes::isEvaluableLine(QStringLiteral("a / b")));
    }

    void testNormalization()
    {
        QCOMPARE(MathNotes::normalizeExpression(QStringLiteral("15% de 240")), QStringLiteral("15% * 240"));
        QCOMPARE(MathNotes::normalizeExpression(QStringLiteral("15 % of 240")), QStringLiteral("15% * 240"));
        QCOMPARE(MathNotes::normalizeExpression(QStringLiteral("12,5 * 2 =")), QStringLiteral("12.5 * 2"));
        QCOMPARE(MathNotes::normalizeExpression(QStringLiteral("  sqrt(9)  ")), QStringLiteral("sqrt(9)"));
    }

    void testOutputParsing()
    {
        const QString out = QStringLiteral("\n> a=12.5\n\n  \x1b[0;36m12.5\x1b[0m\n\n> a*8\n\n  100\n\n");
        const QStringList r = MathNotes::parseQalcOutput(out, {QStringLiteral("a=12.5"), QStringLiteral("a*8")});
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.at(0), QStringLiteral("12.5"));
        QCOMPARE(r.at(1), QStringLiteral("100"));
        // A missing echo yields an empty result instead of shifting the others
        const QStringList miss = MathNotes::parseQalcOutput(out, {QStringLiteral("zzz"), QStringLiteral("a*8")});
        QCOMPARE(miss.at(0), QString());
    }

    void testLiveEvaluationWithVariables()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("qalc")).isEmpty()) {
            QSKIP("qalc not installed");
        }
        const QString notes = QStringLiteral(
            "# Presupuesto\n"
            "precio = 12,5\n"
            "cantidad = 8\n"
            "\n"
            "Total del pedido\n"
            "total = precio * cantidad\n"
            "15% de total\n"
            "sqrt(144) + 3\n");
        const QStringList r = MathNotes::evaluateLines(notes);
        QCOMPARE(r.size(), 9); // 8 lines + trailing empty after final newline
        QCOMPARE(r.at(0), QString());                  // comment
        QCOMPARE(r.at(1), QStringLiteral("12.5"));     // Spanish decimal comma handled
        QCOMPARE(r.at(2), QStringLiteral("8"));
        QCOMPARE(r.at(3), QString());                  // blank
        QCOMPARE(r.at(4), QString());                  // prose is not evaluated
        QCOMPARE(r.at(5), QStringLiteral("100"));      // variables persist across lines
        QVERIFY2(r.at(6).startsWith(QStringLiteral("15")), qPrintable(r.at(6))); // 15% * 100 = 15
        QCOMPARE(r.at(7), QStringLiteral("15"));
    }

    void testEmptyAndProseOnly()
    {
        QCOMPARE(MathNotes::evaluateLines(QString()).size(), 1);
        const QStringList r = MathNotes::evaluateLines(QStringLiteral("hola\nsolo texto"));
        QCOMPARE(r, QStringList({QString(), QString()}));
    }
};

QTEST_MAIN(TestMathNotes)
#include "test_mathnotes.moc"
