/***********************************************************************
 * SPDX-FileCopyrightText: 2003-2004 Max Howell <max.howell@methylblue.com>
 * SPDX-FileCopyrightText: 2008-2009 Martin Sandsmark <martin.sandsmark@kde.org>
 * SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 ***********************************************************************/

#include "filelight_debug.h"

#include <array>
#include <cmath>
#include <numeric>
#include <utility>

#ifdef Q_OS_WINDOWS
#include <winrt/Windows.UI.ViewManagement.h>
#pragma comment(lib, "windowsapp")
#endif

#include <QApplication> //make()
#include <QFont> //ctor
#include <QFontMetrics> //ctor
#include <QImage> //make() & paint()
#include <QPainter>

#include <KColorScheme>
#include <KLocalizedString>

#include "map.h"
#include "memory.h"
#include "radialMap.h" // defines
#include "treemap.h"

RadialMap::Map::Map()
    : m_visibleDepth(DEFAULT_RING_DEPTH)
    , m_ringBreadth(MIN_RING_BREADTH)
    , m_innerRadius(0)
{
    // FIXME this is all broken. No longer is a maximum depth!
    const int fmh = QFontMetrics(QFont()).height();
    const int fmhD4 = fmh / 4;
    MAP_2MARGIN = 2 * (fmh - (fmhD4 - LABEL_MAP_SPACER)); // margin is dependent on fitting in labels at top and bottom
    // Initialize breadth
    resize(QRectF());

    qGuiApp->installEventFilter(this);

    m_treemapRebuildTimer.setSingleShot(true);
    m_treemapRebuildTimer.setInterval(250);
    connect(&m_treemapRebuildTimer, &QTimer::timeout, this, [this] {
        if (m_root && Config::instance()->mapStyle == Filelight::Treemap) {
            make(m_root, true);
        }
    });
}

bool RadialMap::Map::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);
    if (event->type() == QEvent::ApplicationPaletteChange) {
        colorise();
    }
    return false;
}

// Helps to represent a group of files like a single segment on the map
class FilesGroup : public File
{
public:
    FilesGroup(int fileCount, FileSize totalSize, Folder *parent)
        : File("", totalSize, parent)
    {
        const QString fakeName = i18np("\n%1 file, with an average size of %2",
                                       "\n%1 files, with an average size of %2",
                                       fileCount,
                                       KFormat().formatByteSize(totalSize / fileCount));
        m_name = fakeName.toUtf8().constData();
    };

    [[nodiscard]] bool isFilesGroup() const override
    {
        return true;
    }
};

namespace
{
// Items are only shown in the treemap if their tile would be at least this many pixels wide and tall.
// The size limit of the radial map is far stricter, because ring segments need more room to be legible.
constexpr qreal TREEMAP_MIN_TILE = 8;
// Every tile is an item in the scene, so too many of them make the map sluggish.
constexpr int TREEMAP_MAX_TILES = 2000;

enum class Delete {
    Now = true,
    Later = false
};
void deleteAllSegments(QList<QList<RadialMap::Segment *>> &signature, Delete del = Delete::Later)
{
    for (auto &segments : signature) {
        for (const auto &segment : segments) {
            if (segment->file()) {
                Q_ASSERT(segment->file()->segment() == segment->uuid());
                segment->file()->setSegment({});
            }
            del == Delete::Now ? delete segment : segment->deleteLater();
        }
    }
    signature.clear();
}

void clearAllFileSegments(const std::shared_ptr<Folder> &folder)
{
    folder->setSegment({});
    for (const auto &file : folder->files) {
        file->setSegment({});
        if (file->isFolder()) {
            clearAllFileSegments(std::dynamic_pointer_cast<Folder>(file));
        }
    }
}
} // namespace

RadialMap::Map::~Map()
{
    deleteAllSegments(m_signature, Delete::Now);
}

void RadialMap::Map::invalidate()
{
    deleteAllSegments(m_signature);
    Q_EMIT signatureChanged();

    m_visibleDepth = Config::instance()->defaultRingDepth;
}

void RadialMap::Map::make(const std::shared_ptr<Folder> &tree, bool refresh)
{
    // slow operation so set the wait cursor
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // build a signature of visible components
    {
        //**** REMOVE NEED FOR the +1 with MAX_RING_DEPTH uses
        //**** add some angle bounds checking (possibly in Segment ctor? can I delete in a ctor?)
        //**** this is a mess

        deleteAllSegments(m_signature);

        // Clear all file segments in the tree to ensure clean state for rebuilding
        clearAllFileSegments(tree);

        m_root = tree;
        if (!refresh) {
            m_minSize = (tree->size() * 3) / (PI * height() - MAP_2MARGIN);
            findVisibleDepth(tree);
        }

        m_signature.resize(m_visibleDepth + 1);

        if (m_rootSegment && m_rootSegment->file()) {
            m_rootSegment->file()->setSegment({});
        }
        m_rootSegment = make_shared_qobject<Segment>(tree, 0, MAX_DEGREE);
        m_rootSegment->m_treemapExpandable = true;

        setRingBreadth();

        // Calculate ring size limits
        m_limits.resize(m_visibleDepth + 1);
        const double size = m_root->size();
        const double pi2B = M_PI * 4 * m_ringBreadth;
        for (uint depth = 0; depth <= m_visibleDepth; ++depth) {
            m_limits[depth] = uint(size / double(pi2B * (depth + 1))); // min is angle that gives 3px outer diameter for that depth
        }

        if (Config::instance()->mapStyle == Filelight::Treemap) {
            buildTreemap();
        } else {
            build(tree, m_rootSegment.get());
        }
    }

    // colour the segments
    colorise();

    // Publish the complete map, including an empty root and any grouped files.
    Q_EMIT signatureChanged();

    QApplication::restoreOverrideCursor();
}

void RadialMap::Map::setRingBreadth()
{
    // FIXME called too many times on creation

    m_ringBreadth = (height() - MAP_2MARGIN) / (2 * m_visibleDepth + 4);
    m_ringBreadth = qBound(MIN_RING_BREADTH, m_ringBreadth, MAX_RING_BREADTH);
}

void RadialMap::Map::findVisibleDepth(const std::shared_ptr<Folder> &dir, uint currentDepth)
{
    //**** because I don't use the same minimumSize criteria as in the visual function
    //     this can lead to incorrect visual representation
    //**** BUT, you can't set those limits until you know m_depth!

    //**** also this function doesn't check to see if anything is actually visible
    //     it just assumes that when it reaches a new level everything in it is visible
    //     automatically. This isn't right especially as there might be no files in the
    //     dir provided to this function!

    static uint stopDepth = 0;

    if (dir == m_root) {
        stopDepth = Config::instance()->defaultRingDepth;
        m_visibleDepth = 0;
    }

    if (m_visibleDepth < currentDepth) {
        qCDebug(FILELIGHT_LOG) << "changing visual depth" << m_visibleDepth << currentDepth;
        m_visibleDepth = currentDepth;
    }
    if (m_visibleDepth >= stopDepth) {
        return;
    }

    for (const auto &file : dir->files) {
        if (file->isFolder() && file->size() > m_minSize) {
            findVisibleDepth(std::dynamic_pointer_cast<Folder>(file), currentDepth + 1); // if no files greater than min size the depth is still recorded
        }
    }
}

//**** segments currently overlap at edges (i.e. end of first is start of next)
bool RadialMap::Map::build(const std::shared_ptr<Folder> &dir, Segment *parent, const uint depth, uint a_start, const uint a_end)
{
    // first iteration: dir == m_root

    // Prevent division by zero when calculating angles; the children may all be empty directories.
    if (dir->children() == 0 || m_root->size() == 0) {
        return false;
    }

    FileSize hiddenSize = 0;
    uint hiddenFileCount = 0;
    std::vector<std::shared_ptr<File>> hiddenFiles;

    for (const auto &file : dir->files) {
        if (file->size() < m_limits[depth] * 6) { // limit is half a degree? we want at least 3 degrees
            hiddenSize += file->size();
            hiddenFiles.push_back(file);
            if (file->isFolder()) { //**** considered virtual, but dir wouldn't count itself!
                hiddenFileCount += std::dynamic_pointer_cast<Folder>(file)->children(); // need to add one to count the dir as well
            }
            ++hiddenFileCount;
            continue;
        }

        auto a_len = (unsigned int)(MAX_DEGREE * ((double)file->size() / (double)m_root->size()));

        auto *s = new Segment(file, a_start, a_len);
        m_signature[depth].append(s);
        parent->m_children.append(s);

        if (file->isFolder()) {
            if (depth != m_visibleDepth) {
                // recurse
                s->m_hasHiddenChildren = build(std::dynamic_pointer_cast<Folder>(file), s, depth + 1, a_start, a_start + a_len);
            } else {
                s->m_hasHiddenChildren = true;
            }
        }

        a_start += a_len; //**** should we add 1?
    }
    if (hiddenFileCount == dir->children() && !Config::instance()->showSmallFiles) {
        return true;
    }

    if ((depth == 0 || Config::instance()->showSmallFiles) && hiddenSize >= m_limits[depth] && hiddenFileCount > 0) {
        auto *filesGroupSegment = new Segment(std::make_shared<FilesGroup>(hiddenFileCount, hiddenSize, dir.get()), a_start, a_end - a_start, true);
        m_signature[depth].append(filesGroupSegment);
        parent->m_children.append(filesGroupSegment);
        // Assign the FilesGroup segment's UUID to all hidden files so they can highlight the group when hovered in the list
        for (const auto &hiddenFile : hiddenFiles) {
            hiddenFile->setSegment(filesGroupSegment->uuid());
        }
    }

    return false;
}

bool RadialMap::Map::resize(const QRectF &newRect)
{
    // there's a MAP_2MARGIN border

    if (newRect.width() < width() && newRect.height() < height() && !newRect.contains(m_rect)) {
        return false;
    }

    qreal size = qMin(newRect.width(), newRect.height()) - MAP_2MARGIN;

    // this also causes uneven sizes to always resize when resizing but map is small in that dimension
    // size -= size % 2; //even sizes mean less staggered non-antialiased resizing

    const uint minSize = MIN_RING_BREADTH * 2 * (m_visibleDepth + 2);

    if (size < minSize) {
        size = minSize;
    }

    // this QRectF is used by paint()
    m_rect.setRect(0, 0, size, size);
    Q_EMIT rectChanged();

    // resize the pixmap
    size += MAP_2MARGIN;

    if (!m_signature.isEmpty()) {
        setRingBreadth();
    }

    return true;
}

void RadialMap::Map::colorise()
{
    if (m_signature.isEmpty()) {
        qCDebug(FILELIGHT_LOG) << "no signature yet";
        return;
    }

    QColor cp;
    QColor cb;
    double darkness = 1;
    double contrast = (double)Config::instance()->contrast / (double)100;
    int h = 0;
    int s1 = 0;
    int s2 = 0;
    int v1 = 0;
    int v2 = 0;

    QPalette palette = qGuiApp->palette();
#ifdef Q_OS_WINDOWS
    winrt::Windows::UI::ViewManagement::UISettings settings;
    winrt::Windows::UI::Color color = settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
    const QColor kdeColour[2] = {palette.window().color(), QColor(color.R, color.G, color.B, color.A)};
#else
    const QColor kdeColour[2] = {palette.windowText().color(), palette.window().color()};
#endif

    double deltaRed = (double)(kdeColour[0].red() - kdeColour[1].red()) / 2880; // 2880 for semicircle
    double deltaGreen = (double)(kdeColour[0].green() - kdeColour[1].green()) / 2880;
    double deltaBlue = (double)(kdeColour[0].blue() - kdeColour[1].blue()) / 2880;

    for (uint i = 0; i <= m_visibleDepth; ++i, darkness += 0.04) {
        for (const auto &segment : std::as_const(m_signature[i])) {
            switch (Config::instance()->scheme) {
            case Filelight::KDE: {
                // gradient will work by figuring out rgb delta values for 360 degrees
                // then each component is angle*delta

                int a = segment->start();

                if (a > 2880) {
                    a = 2880 - (a - 2880);
                }

                h = (int)(deltaRed * a) + kdeColour[1].red();
                s1 = (int)(deltaGreen * a) + kdeColour[1].green();
                v1 = (int)(deltaBlue * a) + kdeColour[1].blue();

                cb.setRgb(h, s1, v1);
                cb.getHsv(&h, &s1, &v1);

                break;
            }

            case Filelight::HighContrast:
                cp.setHsv(0, 0, 0); // values of h, s and v are irrelevant
                cb.setHsv(180, 0, int(255.0 * contrast));
                segment->setPalette(cp, cb);
                continue;

            default:
                h = int(segment->start() / DEGREE_FACTOR);
                s1 = 160;
                v1 = (int)(255.0 / darkness); //****doing this more often than once seems daft!
            }

            v2 = v1 - int(contrast * v1);
            s2 = s1 + int(contrast * (255 - s1));

            if (s1 < 80) {
                s1 = 80; // can fall too low and makes contrast between the files hard to discern
            }

            if (segment->isFake()) { // multi-file
                cb.setHsv(h, s2, (v2 < 90) ? 90 : v2); // too dark if < 100
                cp.setHsv(h, 17, v1);
            } else if (!segment->file()->isFolder()) { // file
                cb.setHsv(h, 17, v1);
                cp.setHsv(h, 17, v2);
            } else { // folder
                cb.setHsv(h, s1, v1); // v was 225
                cp.setHsv(h, s2, v2); // v was 225 - delta
            }

            segment->setPalette(cp, cb);

            // TODO:
            //**** may be better to store KDE colours as H and S and vary V as others
            //**** perhaps make saturation difference for s2 dependent on contrast too
            //**** fake segments don't work with highContrast
            //**** may work better with cp = cb rather than Qt::white
            //**** you have to ensure the grey of files is sufficient, currently it works only with rainbow (perhaps use contrast there too)
            //**** change v1,v2 to vp, vb etc.
            //**** using percentages is not strictly correct as the eye doesn't work like that
            //**** darkness factor is not done for kde_colour scheme, and also value for files is incorrect really for files in this scheme as it is not set
            // like rainbow one is
        }
    }
}

QList<QVariant> RadialMap::Map::signature()
{
    QList<QVariant> ret;

    for (auto &list : m_signature) {
        QList<QObject *> r;

        for (auto &element : list) {
            r << element;
        }
        ret << QVariant::fromValue(r);
        // break;
    }
    return ret;
}

void RadialMap::Map::zoomIn() // slot
{
    if (m_visibleDepth > MIN_RING_DEPTH) {
        --m_visibleDepth;
        Config::instance()->defaultRingDepth = m_visibleDepth;
        make(m_root);
    }
}

void RadialMap::Map::zoomOut() // slot
{
    ++m_visibleDepth;
    if (m_visibleDepth > Config::instance()->defaultRingDepth) {
        Config::instance()->defaultRingDepth = m_visibleDepth;
    }
    make(m_root);
}

void RadialMap::Map::refresh(const Filelight::Dirty filth)
{
    // TODO consider a more direct connection

    if (!isNull()) {
        switch (filth) {
        case Filelight::Dirty::Layout:
            make(m_root, true); // true means refresh only
            break;
        case Filelight::Dirty::Colors:
            colorise();
            break;
        }
    }
}

void RadialMap::Map::createFromCache(const std::shared_ptr<Folder> &tree)
{
    qCDebug(FILELIGHT_LOG) << "Creating cached tree";
    // no scan was necessary, use cached tree, however we MUST still emit invalidate
    invalidate();
    make(tree);
}

void RadialMap::Map::createFromCacheObject(RadialMap::Segment *segment)
{
    createFromCache(std::dynamic_pointer_cast<Folder>(segment->file()));
}

QUrl RadialMap::Map::rootUrl() const
{
    return m_root ? m_root->url() : QUrl();
}

QObject *RadialMap::Map::rootSegment() const
{
    return m_rootSegment.get();
}

namespace
{
void collectTreemapSizes(const std::shared_ptr<Folder> &dir, uint depth, uint maxDepth, FileSize minimumSize, std::vector<FileSize> &sizes)
{
    for (const auto &file : dir->files) {
        if (file->size() < minimumSize) {
            continue;
        }
        sizes.push_back(file->size());
        if (file->isFolder() && depth < maxDepth) {
            collectTreemapSizes(std::static_pointer_cast<Folder>(file), depth + 1, maxDepth, minimumSize, sizes);
        }
    }
}
} // namespace

FileSize RadialMap::Map::treemapMinimumSize() const
{
    const qreal area = m_treemapSize.width() * m_treemapSize.height();
    if (area <= 0 || !m_root) {
        return 1;
    }
    auto minimumSize = std::max<FileSize>(1, FileSize(std::ceil(m_root->size() * TREEMAP_MIN_TILE * TREEMAP_MIN_TILE / area)));

    // If too many items are large enough, only show the largest ones.
    // Every item counted here covers at least a tile of the view, so there are only so many of them.
    std::vector<FileSize> sizes;
    collectTreemapSizes(m_root, 0, m_visibleDepth, minimumSize, sizes);
    if (sizes.size() > size_t(TREEMAP_MAX_TILES)) {
        auto nth = sizes.begin() + (TREEMAP_MAX_TILES - 1);
        std::nth_element(sizes.begin(), nth, sizes.end(), std::greater<>());
        minimumSize = std::max(minimumSize, *nth);
    }
    return minimumSize;
}

void RadialMap::Map::buildTreemap()
{
    m_treemapRebuildTimer.stop();
    if (m_treemapSize.isEmpty() || m_root->size() == 0) {
        // Nothing to lay out yet, the view will tell its size.
        m_treemapBuiltSize = QSizeF();
        return;
    }
    int budget = TREEMAP_MAX_TILES;
    placeTreemap(m_rootSegment.get(), QRectF(QPointF(0, 0), m_treemapSize), 0, true, treemapMinimumSize(), budget);
    m_treemapBuiltSize = m_treemapSize;
}

namespace
{
void collapseTreemap(RadialMap::Segment *segment, const QPointF &at)
{
    segment->setTreemapGeometry(QRectF(at, QSizeF(0, 0)), 0, false);
    for (auto *child : segment->children()) {
        collapseTreemap(child, at);
    }
}
} // namespace

// Lays out the children of a segment and recurses into them.
// When building, segments are created for exactly the tiles that are placed,
// otherwise the existing segments are moved.
// Returns whether a rebuild would show a different set of tiles.
bool RadialMap::Map::placeTreemap(Segment *segment, const QRectF &rect, const uint childDepth, const bool build, const FileSize minimumSize, int &budget)
{
    const auto folder = std::dynamic_pointer_cast<Folder>(segment->file());
    const FileSize size = segment->file()->size();

    // Only nest the children if there is room for a header with the name and for the children themselves.
    // Otherwise the folder is shown like a file.
    const bool fits = rect.width() >= 3 * m_treemapHeaderHeight && rect.height() >= 2.5 * m_treemapHeaderHeight;
    const bool container = fits && size > 0 && (build ? segment->m_treemapExpandable : !segment->m_children.isEmpty());
    if (!container) {
        segment->setTreemapGeometry(rect, 0, false);
        for (auto *child : std::as_const(segment->m_children)) {
            collapseTreemap(child, rect.center());
        }
        // A rebuild would drop the collapsed children, or show children that now fit.
        return !build && (!segment->m_children.isEmpty() || (fits && segment->m_treemapExpandable));
    }

    const QRectF content = rect.adjusted(m_treemapPadding, m_treemapHeaderHeight, -m_treemapPadding, -m_treemapPadding);

    // The items to lay out, largest first as squarifying needs them.
    struct Item {
        std::shared_ptr<File> file;
        Segment *segment;
        uint start;
        uint length;
    };
    std::vector<Item> items;
    if (build) {
        uint a_start = segment->start();
        for (const auto &file : folder->files) {
            // The angle only determines the color.
            // Advance it for hidden items too, so colors do not shift when the view is resized.
            const auto a_len = uint(MAX_DEGREE * (double(file->size()) / double(m_root->size())));
            if (file->size() >= minimumSize) {
                items.push_back({file, nullptr, a_start, a_len});
            }
            a_start += a_len;
        }
    } else {
        for (auto *child : std::as_const(segment->m_children)) {
            items.push_back({child->file(), child, 0, 0});
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.file->size() > b.file->size();
    });

    QList<qreal> weights;
    weights.reserve(qsizetype(items.size()));
    FileSize itemsSize = 0;
    for (const auto &item : items) {
        weights.append(qreal(item.file->size()));
        itemsSize += item.file->size();
    }

    // Items too small to get a tile are gathered in the bottom right corner where the smallest tiles would be.
    // The minimum area includes the spacing, which is taken off every tile.
    const qreal minimumSide = TREEMAP_MIN_TILE + m_treemapSpacing;
    const qreal minimumArea = minimumSide * minimumSide;
    const qreal hiddenSize = itemsSize < size ? qreal(size - itemsSize) : 0;
    const auto layout = Treemap::squarify(weights, content, hiddenSize, minimumArea);

    const qreal halfSpacing = m_treemapSpacing / 2;
    auto separate = [&](QRectF childRect) {
        // Separate siblings, but never let a rectangle vanish because of it.
        if (childRect.width() > m_treemapSpacing && childRect.height() > m_treemapSpacing) {
            childRect.adjust(halfSpacing, halfSpacing, -halfSpacing, -halfSpacing);
        }
        return childRect;
    };

    bool changed = false;
    FileSize placedSize = 0;
    for (qsizetype i = 0; i < qsizetype(items.size()); ++i) {
        const auto &item = items[size_t(i)];
        if (i >= layout.placed) {
            if (!build) {
                // Too small now, a rebuild would drop it.
                collapseTreemap(item.segment, layout.remainder.center());
                changed = true;
            }
            continue;
        }

        Segment *child = item.segment;
        if (build) {
            if (budget <= 0) {
                break;
            }
            --budget;
            child = new Segment(item.file, item.start, item.length);
            m_signature[childDepth].append(child);
            segment->m_children.append(child);
            if (const auto childFolder = std::dynamic_pointer_cast<Folder>(item.file)) {
                // Whether the folder could show children in a larger view.
                const bool hasLargeChildren = std::any_of(childFolder->files.cbegin(), childFolder->files.cend(), [minimumSize](const auto &file) {
                    return file->size() >= minimumSize;
                });
                child->m_treemapExpandable = childDepth < m_visibleDepth && hasLargeChildren;
                child->m_hasHiddenChildren = !childFolder->files.isEmpty();
            }
        }
        placedSize += item.file->size();
        changed |= placeTreemap(child, separate(layout.rects[i]), childDepth + 1, build, minimumSize, budget);
    }

    if (build) {
        // The largest item left out only because it was too small for this view.
        segment->m_treemapLargestHidden = layout.placed < weights.size() ? FileSize(weights[layout.placed]) : 0;
        segment->m_hasHiddenChildren = segment->m_children.size() < folder->files.size();
    } else if (segment->m_treemapLargestHidden > 0) {
        // A rebuild would show the largest hidden item if it fits now.
        const qreal areaPerByte = content.width() * content.height() / qreal(size);
        changed |= qreal(segment->m_treemapLargestHidden) * areaPerByte >= minimumArea;
    }

    const qreal remainderSize = qreal(size - std::min(placedSize, size));
    const QRectF remainderRect = remainderSize > 0 ? separate(layout.remainder).translated(-rect.topLeft()) : QRectF();
    segment->setTreemapGeometry(rect, m_treemapHeaderHeight, true, remainderRect, remainderSize);
    return changed;
}

void RadialMap::Map::layoutTreemap(qreal width, qreal height, qreal headerHeight, qreal padding, qreal spacing)
{
    if (!m_root || Config::instance()->mapStyle != Filelight::Treemap || width <= 0 || height <= 0) {
        return;
    }

    const bool styleChanged = headerHeight != m_treemapHeaderHeight || padding != m_treemapPadding || spacing != m_treemapSpacing;
    m_treemapHeaderHeight = headerHeight;
    m_treemapPadding = padding;
    m_treemapSpacing = spacing;
    m_treemapSize = QSizeF(width, height);

    // Rebuild right away if there is nothing usable yet, e.g. when the view first appears or is maximized.
    const qreal area = width * height;
    const qreal builtArea = m_treemapBuiltSize.width() * m_treemapBuiltSize.height();
    if (styleChanged || builtArea <= 0 || area > builtArea * 1.5 || area < builtArea / 1.5) {
        make(m_root, true);
        return;
    }

    if (!m_rootSegment || isNull()) {
        return;
    }
    // Otherwise move the existing tiles and only rebuild once resizing settled, if that would show different tiles.
    int budget = 0;
    const bool changed = placeTreemap(m_rootSegment.get(), QRectF(0, 0, width, height), 0, false, 0, budget);
    if (changed) {
        m_treemapRebuildTimer.start();
    }
}

RadialMap::Map *RadialMap::Map::create(QQmlEngine *qml, QJSEngine *js)
{
    auto map = instance();
    QQmlEngine::setObjectOwnership(map, QQmlEngine::CppOwnership);
    return map;
}
