// SPDX-FileCopyrightText: 2026 Septatrix
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "treemap.h"

#include <QTest>

#include <algorithm>
#include <numeric>

class TreemapTest : public QObject
{
    Q_OBJECT

    static qreal area(const QRectF &rect)
    {
        return rect.width() * rect.height();
    }

    static qreal aspect(const QRectF &rect)
    {
        return std::max(rect.width() / rect.height(), rect.height() / rect.width());
    }

    static bool overlaps(const QRectF &a, const QRectF &b)
    {
        constexpr qreal epsilon = 1e-6;
        return a.adjusted(epsilon, epsilon, -epsilon, -epsilon).intersects(b);
    }

    static bool fuzzyEqual(qreal a, qreal b, qreal scale)
    {
        return qAbs(a - b) < 1e-6 * scale;
    }

private Q_SLOTS:
    void fillsBoundsProportionally_data()
    {
        QTest::addColumn<QList<qreal>>("weights");
        QTest::addColumn<qreal>("remainder");
        QTest::addColumn<QRectF>("bounds");
        QTest::newRow("single") << QList<qreal>{42} << 0.0 << QRectF(0, 0, 300, 200);
        QTest::newRow("equal") << QList<qreal>{1, 1, 1, 1} << 0.0 << QRectF(0, 0, 400, 400);
        QTest::newRow("bruls") << QList<qreal>{6, 6, 4, 3, 2, 2, 1} << 0.0 << QRectF(0, 0, 600, 400);
        QTest::newRow("tall-offset") << QList<qreal>{500, 300, 100, 50, 25, 25} << 0.0 << QRectF(10, 20, 120, 800);
        QTest::newRow("long-tail") << QList<qreal>{1000, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 1, 1} << 0.0 << QRectF(0, 0, 1000, 600);
        QTest::newRow("remainder") << QList<qreal>{6, 6, 4, 3} << 5.0 << QRectF(0, 0, 600, 400);
        QTest::newRow("large-remainder") << QList<qreal>{2, 1} << 20.0 << QRectF(5, 5, 300, 300);
    }

    void fillsBoundsProportionally()
    {
        QFETCH(QList<qreal>, weights);
        QFETCH(qreal, remainder);
        QFETCH(QRectF, bounds);

        const auto layout = Treemap::squarify(weights, bounds, remainder);
        QCOMPARE(layout.rects.size(), weights.size());
        QCOMPARE(layout.placed, weights.size());

        const qreal total = std::accumulate(weights.cbegin(), weights.cend(), remainder);
        const qreal boundsArea = area(bounds);
        qreal coveredArea = area(layout.remainder);
        QVERIFY(fuzzyEqual(area(layout.remainder), boundsArea * remainder / total, boundsArea));
        for (qsizetype i = 0; i < layout.rects.size(); ++i) {
            const auto &rect = layout.rects[i];
            QVERIFY2(bounds.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(rect), qPrintable(QStringLiteral("rect %1 is out of bounds").arg(i)));
            QVERIFY(fuzzyEqual(area(rect), boundsArea * weights[i] / total, boundsArea));
            QVERIFY2(!overlaps(rect, layout.remainder), qPrintable(QStringLiteral("rect %1 overlaps the remainder").arg(i)));
            for (qsizetype j = i + 1; j < layout.rects.size(); ++j) {
                QVERIFY2(!overlaps(rect, layout.rects[j]), qPrintable(QStringLiteral("rects %1 and %2 overlap").arg(i).arg(j)));
            }
            coveredArea += area(rect);
        }
        QVERIFY(fuzzyEqual(coveredArea, boundsArea, boundsArea));
    }

    void remainderIsInTheBottomRightCorner()
    {
        const QRectF bounds(10, 10, 600, 400);
        const auto layout = Treemap::squarify({50, 30, 20, 10, 5, 5}, bounds, 15);
        QVERIFY(area(layout.remainder) > 0);
        QCOMPARE(layout.remainder.bottomRight(), bounds.bottomRight());
    }

    void stopsAtTooSmallItems()
    {
        // In 100x100 with a total weight of 100, every unit of weight covers 100 square pixels.
        const QRectF bounds(0, 0, 100, 100);
        const QList<qreal> weights{50, 20, 10, 5, 4, 3, 2, 1};
        const auto layout = Treemap::squarify(weights, bounds, 5, 500);
        // Only the items of at least 500 square pixels are placed.
        QCOMPARE(layout.placed, 4);
        for (qsizetype i = 0; i < weights.size(); ++i) {
            QCOMPARE(area(layout.rects[i]), i < 4 ? weights[i] * 100 : 0.0);
        }
        // The rest, including the reserved remainder, ends up in the corner.
        QVERIFY(fuzzyEqual(area(layout.remainder), (4 + 3 + 2 + 1 + 5) * 100, area(bounds)));
        QCOMPARE(layout.remainder.bottomRight(), bounds.bottomRight());
    }

    void producesReadableAspectRatios()
    {
        // The example from the paper. Slicing this into strips would give an aspect ratio of up to 36,
        // a squarified layout stays below 3.
        const QList<qreal> weights{6, 6, 4, 3, 2, 2, 1};
        const auto layout = Treemap::squarify(weights, QRectF(0, 0, 600, 400));
        for (const auto &rect : layout.rects) {
            QVERIFY2(aspect(rect) < 3, qPrintable(QStringLiteral("aspect ratio %1").arg(aspect(rect))));
        }
    }

    void equalWeightsFormAGrid()
    {
        // A grid of squares is closer to the target ratio than any arrangement of strips.
        const auto layout = Treemap::squarify({1, 1, 1, 1}, QRectF(0, 0, 100, 100));
        for (const auto &rect : layout.rects) {
            QCOMPARE(rect.size(), QSizeF(50, 50));
        }
        QCOMPARE(area(layout.remainder), 0.0);
    }

    void skipsEmptyWeights()
    {
        const auto layout = Treemap::squarify({3, 1, 0, -1}, QRectF(0, 0, 40, 10));
        QCOMPARE(layout.rects.size(), 4);
        QCOMPARE(layout.placed, 2);
        QCOMPARE(area(layout.rects[0]), 300.0);
        QCOMPARE(area(layout.rects[1]), 100.0);
        QCOMPARE(area(layout.rects[2]), 0.0);
        QCOMPARE(area(layout.rects[3]), 0.0);
    }

    void handlesDegenerateBounds()
    {
        const auto layout = Treemap::squarify({3, 1}, QRectF(5, 5, 0, 10));
        QCOMPARE(layout.rects.size(), 2);
        QCOMPARE(layout.placed, 0);
        for (const auto &rect : layout.rects) {
            QCOMPARE(area(rect), 0.0);
        }
        QVERIFY(Treemap::squarify({}, QRectF(0, 0, 10, 10)).rects.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TreemapTest)
#include "treemapTest.moc"
