// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2026 Septatrix

#include "treemap.h"

#include <algorithm>
#include <limits>

namespace
{
// How far the most extreme rectangle of a row deviates from the target aspect ratio.
// 1 is a perfect match, larger is worse.
//
// A row of total weight `rowWeight` occupies a strip along the short side of the free space.
// Its thickness is `rowWeight * scale`
// and a rectangle of weight w in it is `w / rowWeight * shortSide` long.
qreal worstDeviation(qreal minWeight, qreal maxWeight, qreal rowWeight, qreal shortSide, qreal scale, qreal ratio)
{
    const qreal thickness = rowWeight * scale;
    if (thickness <= 0) {
        return std::numeric_limits<qreal>::infinity();
    }
    auto deviation = [&](qreal weight) {
        const qreal aspect = (weight / rowWeight * shortSide) / thickness;
        return std::max(aspect / ratio, ratio / aspect);
    };
    return std::max(deviation(minWeight), deviation(maxWeight));
}
} // namespace

Treemap::Layout Treemap::squarify(const QList<qreal> &weights, const QRectF &bounds, qreal remainderWeight, qreal minimumArea, qreal ratio)
{
    Layout layout;
    layout.rects = QList<QRectF>(weights.size(), QRectF(bounds.topLeft(), QSizeF(0, 0)));
    layout.remainder = bounds;

    qreal total = std::max<qreal>(remainderWeight, 0);
    for (const qreal weight : weights) {
        total += std::max<qreal>(weight, 0);
    }
    if (total <= 0 || bounds.width() <= 0 || bounds.height() <= 0) {
        return layout;
    }

    // Find the items large enough to be placed. As they are sorted, all others are smaller still.
    const qreal areaPerWeight = bounds.width() * bounds.height() / total;
    qsizetype count = 0;
    qreal placedWeight = 0;
    while (count < weights.size() && weights[count] > 0 && weights[count] * areaPerWeight >= minimumArea) {
        placedWeight += weights[count];
        ++count;
    }
    // Everything else is reserved for the remainder.
    const qreal reserved = total - placedWeight;
    qreal remainingWeight = total;

    QRectF free = bounds;
    qsizetype rowBegin = 0;
    while (rowBegin < count && free.width() > 0 && free.height() > 0) {
        // Lay the row along the shorter side so the rectangles stay as square as possible.
        const bool column = free.width() >= free.height();
        const qreal shortSide = column ? free.height() : free.width();
        const qreal longSide = column ? free.width() : free.height();
        // Converts a weight into the thickness of a row spanning the short side.
        const qreal scale = longSide / remainingWeight;

        qreal rowWeight = weights[rowBegin];
        qreal minWeight = rowWeight;
        qreal maxWeight = rowWeight;
        qreal worst = worstDeviation(minWeight, maxWeight, rowWeight, shortSide, scale, ratio);
        qsizetype rowEnd = rowBegin + 1;
        for (; rowEnd < count; ++rowEnd) {
            const qreal weight = weights[rowEnd];
            const qreal candidateMin = std::min(minWeight, weight);
            const qreal candidateMax = std::max(maxWeight, weight);
            const qreal candidate = worstDeviation(candidateMin, candidateMax, rowWeight + weight, shortSide, scale, ratio);
            if (candidate > worst) {
                break;
            }
            worst = candidate;
            rowWeight += weight;
            minWeight = candidateMin;
            maxWeight = candidateMax;
        }

        // Without a remainder the last row takes up whatever is left, so rounding errors cannot leave a gap.
        const bool lastRow = rowEnd == count && reserved <= 0;
        const qreal thickness = lastRow ? longSide : rowWeight * scale;
        qreal offset = 0;
        for (qsizetype i = rowBegin; i < rowEnd; ++i) {
            const qreal length = (i + 1 == rowEnd) ? shortSide - offset : weights[i] / rowWeight * shortSide;
            if (column) {
                layout.rects[i] = QRectF(free.left(), free.top() + offset, thickness, length);
            } else {
                layout.rects[i] = QRectF(free.left() + offset, free.top(), length, thickness);
            }
            offset += length;
        }

        if (column) {
            free.setLeft(free.left() + thickness);
        } else {
            free.setTop(free.top() + thickness);
        }
        remainingWeight -= rowWeight;
        rowBegin = rowEnd;
    }

    layout.placed = rowBegin;
    layout.remainder = reserved > 0 ? free : QRectF(free.bottomRight(), QSizeF(0, 0));
    return layout;
}
