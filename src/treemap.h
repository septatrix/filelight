// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2026 Septatrix

#pragma once

#include <QList>
#include <QRectF>

namespace Treemap
{
// Target aspect ratio of the squarified layout.
// Comparing areas is hardest when both rectangles are squares or both are very elongated
// (Kong, Heer and Agrawala, "Perceptual Guidelines for Creating Rectangular Treemaps", 2010),
// so like d3 aim for the golden ratio in between.
inline constexpr qreal GoldenRatio = 1.618033988749895;

struct Layout {
    // One rectangle per weight, in the same order.
    // Items that were not placed get an empty rectangle.
    QList<QRectF> rects;
    // The number of leading items that were placed.
    qsizetype placed = 0;
    // The space left over for the remainder and the items that were not placed.
    // It is always in the bottom right corner of the bounds.
    QRectF remainder;
};

/**
 * Lays out @p weights inside @p bounds as a squarified treemap
 * (Bruls, Huizing and van Wijk, "Squarified Treemaps", 2000).
 *
 * The area of each rectangle is proportional to its weight.
 * Rectangles are grouped into rows along the shorter side of the remaining space,
 * and a row grows as long as that brings its rectangles closer to @p ratio.
 * Each row is placed at the top or left of the remaining space,
 * so what remains is always a rectangle in the bottom right corner.
 *
 * @p weights must be sorted in descending order.
 * @p remainderWeight is reserved in the remaining space and not placed.
 * Placing stops at the first item whose rectangle would be smaller than @p minimumArea,
 * it and all smaller items become part of the remainder instead.
 * Non-positive weights are never placed.
 */
Layout squarify(const QList<qreal> &weights, const QRectF &bounds, qreal remainderWeight = 0, qreal minimumArea = 0, qreal ratio = GoldenRatio);
} // namespace Treemap
