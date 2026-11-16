// SPDX-FileCopyrightText: 2026 Vladimir Ivanov <me@un1or.name>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#include "Config.h"
#include "fileCleaner.h"
#include "radialMap/map.h"

#include <QScopeGuard>
#include <QStandardPaths>
#include <QTest>

#include <algorithm>
#include <functional>

class MapUpdateTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        FileCleaner::instance();
        Config::instance()->read();
        Config::instance()->scheme = Filelight::Rainbow;
        Config::instance()->contrast = 75;
        Config::instance()->showSmallFiles = false;
    }

    void publishesCompleteMap_data()
    {
        QTest::addColumn<int>("entries");
        QTest::addColumn<int>("segments");
        QTest::newRow("empty-root") << 0 << 0;
        QTest::newRow("empty-child") << 1 << 0;
        QTest::newRow("grouped-files") << 3 << 2;
    }

    void publishesCompleteMap()
    {
        QFETCH(int, entries);
        QFETCH(int, segments);
        auto map = RadialMap::Map::instance();
        const auto previous = std::make_shared<Folder>("previous/");
        previous->append("file", 100);
        map->resize({0, 0, 600, 600});
        map->make(previous);

        const auto root = std::make_shared<Folder>("root/");
        if (entries == 1) {
            root->append(std::make_shared<Folder>("empty/"));
        } else if (entries == 3) {
            root->append("large", 10000);
            root->append("small-a", 30);
            root->append("small-b", 30);
        }

        QObject observer;
        int publications = 0;
        QObject *publishedRoot = nullptr;
        QList<QColor> publishedColors;
        connect(map, &RadialMap::Map::signatureChanged, &observer, [&] {
            ++publications;
            publishedRoot = map->rootSegment();
            for (const auto &ring : map->rawSignature()) {
                for (const auto segment : ring) {
                    publishedColors.append(segment->brush());
                }
            }
        });
        map->make(root);
        QCOMPARE(publications, 1);
        QCOMPARE(publishedRoot, map->rootSegment());
        QCOMPARE(publishedColors.size(), segments);
        QList<QColor> finalColors;
        for (const auto &ring : map->rawSignature()) {
            for (const auto segment : ring) {
                finalColors.append(segment->brush());
            }
        }
        QCOMPARE(publishedColors, finalColors);
    }

    void treemapNestsChildren()
    {
        Config::instance()->mapStyle = Filelight::Treemap;
        auto restoreStyle = qScopeGuard([] {
            Config::instance()->mapStyle = Filelight::Radial;
        });
        auto map = RadialMap::Map::instance();
        const auto root = std::make_shared<Folder>("root/");
        const auto big = std::make_shared<Folder>("big/");
        big->append("a", 40000);
        big->append("b", 20000);
        big->append("tiny", 1);
        root->append(big);
        root->append("file", 30000);
        map->resize({0, 0, 600, 600});
        map->make(root);

        constexpr qreal header = 20;
        constexpr qreal padding = 4;
        map->layoutTreemap(800, 600, header, padding, 2);

        auto rootSegment = qobject_cast<RadialMap::Segment *>(map->rootSegment());
        QVERIFY(rootSegment);
        QCOMPARE(rootSegment->treemapRect(), QRectF(0, 0, 800, 600));
        QVERIFY(rootSegment->isTreemapContainer());

        // Every visible segment lies inside the content area of its parent,
        // and its area is proportional to its size.
        std::function<void(RadialMap::Segment *)> check = [&](RadialMap::Segment *parent) {
            const QRectF content = parent->treemapRect().adjusted(padding, header, -padding, -padding);
            const qreal areaPerByte = content.width() * content.height() / qreal(parent->file()->size());
            for (auto *child : parent->children()) {
                const QRectF rect = child->treemapRect();
                QVERIFY2(content.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(rect), qPrintable(child->displayName()));
                // Spacing shrinks every rectangle a little, so only check roughly.
                QVERIFY(rect.width() * rect.height() <= areaPerByte * child->file()->size() * 1.0001);
                QVERIFY(rect.width() * rect.height() >= areaPerByte * child->file()->size() * 0.9);
                if (child->isTreemapContainer()) {
                    check(child);
                }
            }
        };
        check(rootSegment);

        // The tiny file is too small to be shown and makes up the remainder of its folder.
        const auto bigSegment = std::find_if(rootSegment->children().cbegin(), rootSegment->children().cend(), [&big](auto *segment) {
            return segment->file() == big;
        });
        QVERIFY(bigSegment != rootSegment->children().cend());
        QVERIFY((*bigSegment)->isTreemapContainer());
        QCOMPARE((*bigSegment)->children().size(), 2);
        QCOMPARE((*bigSegment)->treemapRemainderSize(), 1.0);
    }

    void treemapShowsSmallerItemsThanRadialMap()
    {
        const auto root = std::make_shared<Folder>("root/");
        root->append("large", 300000);
        for (int i = 0; i < 100; ++i) {
            root->append(QByteArray("small-" + QByteArray::number(i)).constData(), 1000);
        }
        auto countSegments = [] {
            int count = 0;
            for (const auto &ring : RadialMap::Map::instance()->rawSignature()) {
                count += ring.size();
            }
            return count;
        };

        auto map = RadialMap::Map::instance();
        map->resize({0, 0, 600, 600});
        map->make(root);
        // The radial map shows the large file and groups the small ones.
        QCOMPARE(countSegments(), 2);

        Config::instance()->mapStyle = Filelight::Treemap;
        auto restoreStyle = qScopeGuard([] {
            Config::instance()->mapStyle = Filelight::Radial;
        });
        map->make(root);
        map->layoutTreemap(800, 600, 20, 4, 2);
        // Each small file gets about 1000 square pixels, plenty for a tile.
        QCOMPARE(countSegments(), 101);

        // Every tile gets an item in the scene, so their number is capped.
        const auto huge = std::make_shared<Folder>("huge/");
        for (int i = 0; i < 5000; ++i) {
            huge->append(QByteArray("file-" + QByteArray::number(i)).constData(), 1000);
        }
        map->make(huge);
        map->layoutTreemap(4000, 4000, 20, 4, 2);
        QVERIFY(countSegments() > 0);
        QVERIFY(countSegments() <= 2000);
    }
};

QTEST_MAIN(MapUpdateTest)
#include "mapUpdateTest.moc"
