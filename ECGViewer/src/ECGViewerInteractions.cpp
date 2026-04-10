/**
 * @file ECGViewerInteractions.cpp
 * @brief Mouse/keyboard interaction handling for ECGViewer.
 *
 * This translation unit implements interactive behaviors on the plot:
 * - Dragging fiducial markers (with resampling of cleaned Y values)
 * - Dragging point notes and resizing/moving region notes
 * - Shift+drag region creation
 * - Hover detection and delete shortcuts
 * - Double-click behavior for opening note editors
 *
 * The focus here is translating input events into updates on the backing data
 * (notes_/fiducial vectors) and updating plot items for responsiveness.
 */

#include "ECGViewer.hpp"

#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace ECGViewer {

void ECGViewer::deleteHoveredFiducial()
{
    if (hoverFiducialIndex_ < 0 || hoverFiducialIndex_ >= fiducialsCurrent_.size())
        return;

    const auto f = fiducialsCurrent_[hoverFiducialIndex_];

    auto r = fiducialRefsFor(f.type);
    QVector<double>& times = *r.times;
    QVector<double>& vals = *r.vals;

    if (f.index < 0 || f.index >= times.size())
        return;

    times.remove(f.index);
    vals.remove(f.index);

    refreshFiducialGraph(f.type);

    updateFiducialLines(currentX0, currentX1);

    hoverFiducialIndex_ = -1;

    plot_->replot();
}

void ECGViewer::onPlotMouseDoubleClick(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    QCPAbstractItem* item = plot_->itemAt(event->pos(), true);
    if (!item)
        return;

    for (int i = 0; i < notesCurrent_.size(); ++i) {
        const auto& nv = notesCurrent_[i];
        if (nv.text == item || nv.line == item || nv.rect == item) {
            openNoteEditor(nv.noteIndex);
            return;
        }
    }
}

/**
 * @brief Mouse press begins drags for notes/fiducials or starts region creation (Shift+drag).
 * @details Priority is: shift+note region resize/move, shift+empty => create region,
 * then normal note drag, then fiducial drag.
 */
void ECGViewer::onPlotMousePress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (zoomRectMode_)
        return;

    const bool shiftHeld = (event->modifiers() & Qt::ShiftModifier) != 0;
    QCPAbstractItem* item = plot_->itemAt(event->pos(), true);

    // Overlay mode has priority over Shift+region-note creation.
    if (item) {
        int ovIndex = -1;
        if (overlayIndexFromItem(item, ovIndex)) {
            if ((ovIndex >= 0 && ovIndex < overlays_.size()) && areOverlaysMoveable_) {
                activeOverlayIndex_ = ovIndex;
                overlayDragMode_ = OverlayDragMode::Moving;

                overlayMoveStartX_ = mouseTimeClamped(event);
                overlayMoveOrigDx_ = overlays_[ovIndex].dx;

                beginItemDrag(Qt::SizeHorCursor);
                return;
            }
        }
    }
    if (overlayMode_) {
        if (shiftHeld && !item) {
            overlayDragMode_ = OverlayDragMode::Selecting;
            overlayAnchorX_ = mouseTimeClamped(event);

            updateOverlayRubberBand(overlayAnchorX_, overlayAnchorX_);
            beginItemDrag(Qt::CrossCursor);

            plot_->replot(QCustomPlot::rpQueuedReplot);
            return;
        }
    }


    if (shiftHeld && item) {
        for (int i = 0; i < notesCurrent_.size(); ++i) {
            auto& nv = notesCurrent_[i];
            if (!(nv.line == item || nv.text == item || nv.rect == item))
                continue;

            if (nv.noteIndex < 0 || nv.noteIndex >= notes_.size())
                return;

            Note& n = notes_[nv.noteIndex];
            if (n.duration <= 0.0) {
                openNoteEditor(nv.noteIndex);
                return;
            }

            const double t0 = n.time;
            const double t1 = n.time + n.duration;

            const int px = event->pos().x();
            const int leftPx  = plot_->xAxis->coordToPixel(t0);
            const int rightPx = plot_->xAxis->coordToPixel(t1);

            const int edgeTolPx = 7;

            noteDragMode_ = NoteDragMode::Move;
            if (std::abs(px - leftPx) <= edgeTolPx)
                noteDragMode_ = NoteDragMode::ResizeLeft;
            else if (std::abs(px - rightPx) <= edgeTolPx)
                noteDragMode_ = NoteDragMode::ResizeRight;

            draggingNote_ = true;
            activeNoteVisualIndex_ = i;

            regionPressTime_ = mouseTimeClamped(event);
            originalStart_ = t0;
            originalEnd_ = t1;

            if (noteDragMode_ == NoteDragMode::Move) {
                noteDragOffsetSeconds_ = n.time - plot_->xAxis->pixelToCoord(event->pos().x());
            } else {
                noteDragOffsetSeconds_ = 0.0;
            }

            beginItemDrag(Qt::SizeHorCursor);
            return;
        }
    }

    if (shiftHeld && !item) {
        double clickX = mouseTimeClamped(event);

        creatingRegion_ = true;
        regionAnchorTime_ = clickX;

        Note n;
        n.time = clickX;
        const double eps = minNoteDurationSeconds();
        n.duration = eps;
        n.tag = QStringLiteral("Region %1").arg(notes_.size() + 1);
        n.detail = QString();
        n.volts = cleanValueAtTime(n.time);

        notes_.push_back(n);
        creatingNoteIndex_ = notes_.size() - 1;

        updateNoteItems(currentX0, currentX1);
        for (int i = 0; i < notesCurrent_.size(); ++i) {
            if (notesCurrent_[i].noteIndex == creatingNoteIndex_) {
                activeNoteVisualIndex_ = i;
                break;
            }
        }

        beginItemDrag(Qt::CrossCursor);

        plot_->replot(QCustomPlot::rpQueuedReplot);
        noteDragMode_ = NoteDragMode::CreateRegion;
        return;
    }

    for (int i = 0; i < fiducialsCurrent_.size(); ++i) {
        auto& f = fiducialsCurrent_[i];
        if (f.line == item || f.text == item) {
            draggingFiducial_ = true;
            activeFiducialIndex_ = i;

            double clickX = plot_->xAxis->pixelToCoord(event->pos().x());
            auto r = fiducialRefsFor(f.type);
            double currentX = (*r.times)[f.index];
            dragOffsetSeconds_ = currentX - clickX;

            beginItemDrag(Qt::ClosedHandCursor);
            return;
        }
    }

    if (!item || !areNotesMoveable_)
        return;

    for (int i = 0; i < notesCurrent_.size(); ++i) {
        auto& nv = notesCurrent_[i];
        if (nv.line == item || nv.text == item || nv.rect == item) {
            draggingNote_ = true;
            activeNoteVisualIndex_ = i;
            noteDragMode_ = NoteDragMode::Move;

            double clickX = plot_->xAxis->pixelToCoord(event->pos().x());
            const Note& note = notes_[nv.noteIndex];
            noteDragOffsetSeconds_ = note.time - clickX;

            beginItemDrag(Qt::ClosedHandCursor);
            return;
        }
    }

}

/**
 * @brief Mouse move updates active drags or provides hover feedback.
 * @details Updates plot items in-place during drags for responsiveness, then queues replot.
 */
void ECGViewer::onPlotMouseMove(QMouseEvent* event)
{

    if (overlayDragMode_ == OverlayDragMode::Selecting) {
        const double x = mouseTimeClamped(event);

        updateOverlayRubberBand(overlayAnchorX_, x);
        plot_->replot(QCustomPlot::rpQueuedReplot);
        return;
    }
    if (overlayDragMode_ == OverlayDragMode::Moving &&
        activeOverlayIndex_ >= 0 && activeOverlayIndex_ < overlays_.size())
    {
        const double x = mouseTimeClamped(event);
        const double dx = x - overlayMoveStartX_;

        OverlayVisual& ov = overlays_[activeOverlayIndex_];
        ov.dx = overlayMoveOrigDx_ + dx;

        applyOverlayTransform(ov);

        setCursor(Qt::SizeHorCursor);
        plot_->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    if (creatingRegion_ && creatingNoteIndex_ >= 0 && creatingNoteIndex_ < notes_.size())
    {
        Note& n = notes_[creatingNoteIndex_];

        double mouseX = mouseTimeClamped(event);
        const double eps = minNoteDurationSeconds();

        double t0 = std::min(regionAnchorTime_, mouseX);
        double t1 = std::max(regionAnchorTime_, mouseX);

        n.time = t0;
        n.duration = std::max(eps, t1 - t0);

        if (activeNoteVisualIndex_ >= 0 && activeNoteVisualIndex_ < notesCurrent_.size() &&
            notesCurrent_[activeNoteVisualIndex_].noteIndex == creatingNoteIndex_)
        {
            auto& nv = notesCurrent_[activeNoteVisualIndex_];
            const double yLow  = plot_->yAxis->range().lower;
            const double yHigh = plot_->yAxis->range().upper;

            if (nv.rect) {
                nv.rect->topLeft->setCoords(t0, yHigh);
                nv.rect->bottomRight->setCoords(t1, yLow);
            }
            if (nv.text) {
                nv.text->position->setCoords(t0, yHigh);
            }
        }
        else
        {
            updateNoteItems(currentX0, currentX1);

            activeNoteVisualIndex_ = -1;
            for (int i = 0; i < notesCurrent_.size(); ++i) {
                if (notesCurrent_[i].noteIndex == creatingNoteIndex_) {
                    activeNoteVisualIndex_ = i;
                    break;
                }
            }
        }

        plot_->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    if (draggingNote_ && activeNoteVisualIndex_ >= 0) {
        if (activeNoteVisualIndex_ >= notesCurrent_.size())
            return;

        auto& nv = notesCurrent_[activeNoteVisualIndex_];
        if (nv.noteIndex < 0 || nv.noteIndex >= notes_.size())
            return;

        Note& n = notes_[nv.noteIndex];

        double mouseX = mouseTimeClamped(event);
        const double eps = minNoteDurationSeconds();

        if (noteDragMode_ == NoteDragMode::CreateRegion)
        {
            double t0 = std::min(regionAnchorTime_, mouseX);
            double t1 = std::max(regionAnchorTime_, mouseX);

            n.time = t0;
            n.duration = std::max(eps, t1 - t0);
        }
        else if (noteDragMode_ == NoteDragMode::ResizeLeft)
        {
            double newStart = clampTime(std::min(mouseX, originalEnd_));
            double newEnd   = originalEnd_;

            if (newEnd - newStart < eps)
                newStart = newEnd - eps;

            n.time = newStart;
            n.duration = std::max(eps, newEnd - newStart);
        }
        else if (noteDragMode_ == NoteDragMode::ResizeRight)
        {
            double newStart = originalStart_;
            double newEnd   = clampTime(std::max(mouseX, originalStart_));

            if (newEnd - newStart < eps)
                newEnd = newStart + eps;

            n.time = newStart;
            n.duration = std::max(eps, newEnd - newStart);
        }
        else
        {
            double newStart = clampTime(mouseX + noteDragOffsetSeconds_);

            if (n.duration > 0.0) {
                if (newStart + n.duration > total_time_)
                    newStart = std::max(0.0, total_time_ - n.duration);
            }

            n.time = newStart;
        }

        const double yLow  = plot_->yAxis->range().lower;
        const double yHigh = plot_->yAxis->range().upper;

        if (nv.line) {
            nv.line->start->setCoords(n.time, yLow);
            nv.line->end->setCoords(n.time, yHigh);
        }

        if (nv.rect) {
            nv.rect->topLeft->setCoords(n.time, yHigh);
            nv.rect->bottomRight->setCoords(n.time + n.duration, yLow);
        }

        if (nv.text) {
            nv.text->position->setCoords(n.time, yHigh);
        }

        setCursor(Qt::SizeHorCursor);
        plot_->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    if (draggingFiducial_ && activeFiducialIndex_ >= 0)
    {
        if (activeFiducialIndex_ >= fiducialsCurrent_.size())
            return;

        auto& f = fiducialsCurrent_[activeFiducialIndex_];

        double mouseX = plot_->xAxis->pixelToCoord(event->pos().x());
        double newTime = clampTime(mouseX + dragOffsetSeconds_);

        double yLow  = plot_->yAxis->range().lower;
        double yHigh = plot_->yAxis->range().upper;

        f.line->start->setCoords(newTime, yLow);
        f.line->end->setCoords(newTime, yHigh);
        f.text->position->setCoords(newTime, yHigh);

        auto r = fiducialRefsFor(f.type);
        f.text->setText(QString("%1 @ %2s").arg(r.label).arg(newTime, 0, 'f', 5));

        setCursor(Qt::ClosedHandCursor);

        updatePoint(f, newTime);

        refreshFiducialGraph(f.type);

        plot_->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    if (zoomRectMode_) {
        setCursor(Qt::ArrowCursor);
        hoverFiducialIndex_ = -1;
        hoverNoteIndex_ = -1;
        return;
    }

    QCPAbstractItem* item = plot_->itemAt(event->pos(), true);

    int foundNoteIndex = -1;
    int foundFidIndex  = -1;
    int foundOverlayIndex = -1;

    if (item) {
        // Notes first
        for (int i = 0; i < notesCurrent_.size(); ++i) {
            const auto& nv = notesCurrent_[i];
            if (nv.line == item || nv.text == item || nv.rect == item) {
                foundNoteIndex = i;
                break;
            }
        }

        // Then fiducials
        if (foundNoteIndex < 0) {
            for (int i = 0; i < fiducialsCurrent_.size(); ++i) {
                const auto& f = fiducialsCurrent_[i];
                if (f.line == item || f.text == item) {
                    foundFidIndex = i;
                    break;
                }
            }
        }

        // Then overlays (rects)
        if (foundNoteIndex < 0 && foundFidIndex < 0) {
            overlayIndexFromItem(item, foundOverlayIndex);
        }
    }

    hoverNoteIndex_ = foundNoteIndex;
    hoverFiducialIndex_ = foundFidIndex;
    hoverOverlayIndex_ = foundOverlayIndex;

    if ((hoverNoteIndex_ >= 0 && areNotesMoveable_) || 
        hoverFiducialIndex_ >= 0 || 
        (hoverOverlayIndex_ >= 0 && areOverlaysMoveable_)
    ) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

}

/**
 * @brief Update backing fiducial vectors for a moved fiducial and resample Y.
 */
void ECGViewer::updatePoint(FiducialVisual& f, double newTime)
{
    auto r = fiducialRefsFor(f.type);
    QVector<double>& times = *r.times;
    QVector<double>& vals = *r.vals;

    if (f.index >= 0 && f.index < times.size()) {
        times[f.index] = newTime;
        vals[f.index] = cleanValueAtTime(newTime);
    }
}

/**
 * @brief Mouse release finalizes region creation or completes drags.
 * @details For region creation: tiny regions are optionally collapsed to point notes.
 */
void ECGViewer::onPlotMouseRelease(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (overlayMode_) {
        if (overlayDragMode_ == OverlayDragMode::Selecting) {
            overlayDragMode_ = OverlayDragMode::None;

            const double x = mouseTimeClamped(event);

            if (overlayRubberBand_) {
                overlayRubberBand_->setVisible(false);
            }

            endItemDrag();

            finalizeOverlayFromSelection(overlayAnchorX_, x);
            return;
        }

    }
    if (overlayDragMode_ == OverlayDragMode::Moving) {
        overlayDragMode_ = OverlayDragMode::None;
        activeOverlayIndex_ = -1;

        endItemDrag();
        plot_->replot(QCustomPlot::rpQueuedReplot);
        return;
    }


    if (creatingRegion_) {
        creatingRegion_ = false;

        if (creatingNoteIndex_ >= 0 && creatingNoteIndex_ < notes_.size()) {
            Note& n = notes_[creatingNoteIndex_];

            const double minDur = minNoteDurationSeconds();
            if (n.duration < minDur) {
                n.duration = 0.0;
            }

            clampNoteToBounds(n);

            updateNoteItems(currentX0, currentX1);
            refreshNotesList();
            plot_->replot();

            openNoteEditor(creatingNoteIndex_);
        }

        creatingNoteIndex_ = -1;
        activeNoteVisualIndex_ = -1;
        regionAnchorTime_ = 0.0;

        endItemDrag();
        noteDragMode_ = NoteDragMode::None;
        return;
    }

    if (draggingNote_ && activeNoteVisualIndex_ >= 0) {
        draggingNote_ = false;
        activeNoteVisualIndex_ = -1;
        noteDragOffsetSeconds_ = 0.0;

        endItemDrag();
        plot_->replot();

        noteDragMode_ = NoteDragMode::None;
        noteDragOffsetSeconds_ = 0.0;
        originalStart_ = originalEnd_ = 0.0;
        regionPressTime_ = 0.0;

        return;
    }

    if (!draggingFiducial_ || activeFiducialIndex_ < 0)
        return;

    auto& f = fiducialsCurrent_[activeFiducialIndex_];

    updatePoint(f, f.line->start->coords().x());
    refreshFiducialGraph(f.type);

    draggingFiducial_ = false;
    activeFiducialIndex_ = -1;
    dragOffsetSeconds_ = 0.0;

    endItemDrag();
    plot_->replot();
}


double ECGViewer::mouseVoltsClamped(QMouseEvent* e) const
{
    double y = plot_->yAxis->pixelToCoord(e->pos().y());
    const double yLow = plot_->yAxis->range().lower;
    const double yHigh = plot_->yAxis->range().upper;

    if (y < yLow) y = yLow;
    if (y > yHigh) y = yHigh;
    return y;
}

void ECGViewer::setOverlayMode(bool enabled)
{
    overlayMode_ = enabled;

    if (overlayMode_) {
        // Overlay selection uses Shift+drag; keep normal drag/zoom available.
        // We also avoid fighting with rect-zoom mode.
        if (zoomRectMode_) {
            zoomRectMode_ = false;
            if (btnZoomRect_) btnZoomRect_->setChecked(false);
        }
    }

    overlayDragMode_ = OverlayDragMode::None;
    activeOverlayIndex_ = -1;

    if (overlayRubberBand_) {
        overlayRubberBand_->setVisible(false);
        plot_->replot(QCustomPlot::rpQueuedReplot);
    }
}

void ECGViewer::clearOverlays()
{
    for (auto& ov : overlays_) {
        if (ov.rect) plot_->removeItem(ov.rect);
        ov.rect = nullptr;

        if (ov.graph) {
            plot_->removePlottable(ov.graph);
        }
        ov.graph = nullptr;
    }
    overlays_.clear();

    activeOverlayIndex_ = -1;
    overlayDragMode_ = OverlayDragMode::None;

    if (overlayRubberBand_) {
        overlayRubberBand_->setVisible(false);
    }

    plot_->replot();
}

void ECGViewer::setOverlaysVisible(bool visible)
{
    overlaysVisible_ = visible;

    for (auto& ov : overlays_) {
        ov.visible = visible;
        if (ov.graph) ov.graph->setVisible(visible);
        if (ov.rect) ov.rect->setVisible(visible);
    }

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

bool ECGViewer::overlayIndexFromItem(QCPAbstractItem* item, int& outIndex) const
{
    outIndex = -1;
    if (!item) return false;

    for (int i = 0; i < overlays_.size(); ++i) {
        if (overlays_[i].rect == item) {
            outIndex = i;
            return true;
        }
    }
    return false;
}

void ECGViewer::ensureOverlayRubberBand()
{
    if (overlayRubberBand_) return;

    overlayRubberBand_ = new QCPItemRect(plot_);
    overlayRubberBand_->setClipToAxisRect(true);
    overlayRubberBand_->setSelectable(false);

    QPen pen(QColor(220, 20, 60));
    pen.setWidthF(1.0);
    overlayRubberBand_->setPen(pen);
    overlayRubberBand_->setBrush(QBrush(QColor(220, 20, 60, 40)));
    overlayRubberBand_->setVisible(false);
}

void ECGViewer::updateOverlayRubberBand(double x0, double x1)
{
    ensureOverlayRubberBand();

    const double left = std::min(x0, x1);
    const double right = std::max(x0, x1);

    const double yLow = plot_->yAxis->range().lower;
    const double yHigh = plot_->yAxis->range().upper;

    overlayRubberBand_->topLeft->setCoords(left, yHigh);
    overlayRubberBand_->bottomRight->setCoords(right, yLow);
    overlayRubberBand_->setVisible(true);
}

/**
 * @brief Apply current overlay transform (dx) to the overlay visual elements.
 * @details Updates the graph data and rectangle item positions based on the stored
 * dx offset. Y values remain unchanged.
 * 
 * @param ov OverlayVisual to transform
 * @return void
 */
void ECGViewer::applyOverlayTransform(OverlayVisual& ov)
{
    if (!ov.graph) return;

    QVector<double> x;
    x.resize(ov.baseX.size());

    for (int i = 0; i < ov.baseX.size(); ++i) {
        x[i] = ov.baseX[i] + ov.dx;
    }

    // Y stays exactly the stored base data (no dy)
    ov.graph->setData(x, ov.baseY);

    const double left = std::min(ov.x0, ov.x1) + ov.dx;
    const double right = std::max(ov.x0, ov.x1) + ov.dx;

    const double yLow = plot_->yAxis->range().lower;
    const double yHigh = plot_->yAxis->range().upper;

    if (ov.rect) {
        ov.rect->topLeft->setCoords(left, yHigh);
        ov.rect->bottomRight->setCoords(right, yLow);
    }
}


/**
 * @brief Finalize overlay creation from a selected region.
 * @details Samples the cleaned ECG data over the selected time range,
 * downsampling if necessary, and creates the overlay plot and rectangle items.
 * The overlay is added to the internal list and displayed if overlays are set to visible.
 * Handles edge cases like zero-length selections and ensures proper indexing.
 * Uses vClean_ for overlay data; change to vOrig_ if original signal is desired.
 * 
 * @param x0 Start of selection in relative time (seconds).
 * @param x1 End of selection in relative time (seconds).
 * @return void
 */
void ECGViewer::finalizeOverlayFromSelection(double x0, double x1)
{
    const double left = std::min(x0, x1);
    const double right = std::max(x0, x1);

    if (right - left <= 0.0) {
        return;
    }

    const double tAbs0 = t_.first() + left;
    const double tAbs1 = t_.first() + right;

    int i0 = static_cast<int>(std::floor((tAbs0 - t_.first()) * fs_));
    int i1 = static_cast<int>(std::ceil((tAbs1 - t_.first()) * fs_));

    if (i0 < 0) i0 = 0;
    if (i1 >= t_.size()) i1 = t_.size() - 1;
    if (i1 < i0) std::swap(i0, i1);

    if (i1 - i0 < 2) {
        return;
    }

    OverlayVisual ov;
    ov.x0 = left;
    ov.x1 = right;
    ov.dx = 0.0;
    ov.visible = overlaysVisible_;

    const int rawCount = i1 - i0 + 1;
    const int maxPoints = 8000;
    int step = rawCount > maxPoints ? (rawCount / maxPoints) : 1;
    if (step < 1) step = 1;

    ov.baseX.reserve(rawCount / step + 1);
    ov.baseY.reserve(rawCount / step + 1);

    const double t0 = t_.first();
    for (int i = i0; i <= i1; i += step) {
        const double tRel = t_[i] - t0;
        ov.baseX.push_back(tRel);
        ov.baseY.push_back(vClean_[i]);
    }

    if (ov.baseX.size() < 2) {
        return;
    }

    ov.graph = plot_->addGraph();
    {
        QPen p(QColor(128, 0, 128, 200));
        p.setWidthF(1.6);
        ov.graph->setPen(p);
    }
    ov.graph->setLineStyle(QCPGraph::lsLine);
    ov.graph->setVisible(ov.visible);
    ov.graph->setData(ov.baseX, ov.baseY);

    ov.rect = new QCPItemRect(plot_);
    ov.rect->setClipToAxisRect(true);
    ov.rect->setSelectable(true);
    ov.rect->setVisible(ov.visible);

    QPen rpen(QColor(128, 0, 128));
    rpen.setWidthF(1.2);
    ov.rect->setPen(rpen);
    ov.rect->setBrush(QBrush(QColor(128, 0, 128, 25)));

    const double yLow = plot_->yAxis->range().lower;
    const double yHigh = plot_->yAxis->range().upper;

    ov.rect->topLeft->setCoords(left, yHigh);
    ov.rect->bottomRight->setCoords(right, yLow);

    overlays_.push_back(ov);

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void ECGViewer::deleteHoveredOverlay()
{
    if (hoverOverlayIndex_ < 0 || hoverOverlayIndex_ >= overlays_.size())
        return;

    OverlayVisual& ov = overlays_[hoverOverlayIndex_];

    if (ov.rect) {
        plot_->removeItem(ov.rect);
        ov.rect = nullptr;
    }

    if (ov.graph) {
        plot_->removePlottable(ov.graph);
        ov.graph = nullptr;
    }

    overlays_.remove(hoverOverlayIndex_);

    // Keep active index sane if user was dragging/selected one.
    if (activeOverlayIndex_ == hoverOverlayIndex_) {
        activeOverlayIndex_ = -1;
        overlayDragMode_ = OverlayDragMode::None;
    } else if (activeOverlayIndex_ > hoverOverlayIndex_) {
        activeOverlayIndex_ -= 1;
    }

    hoverOverlayIndex_ = -1;

    plot_->replot();
}

void ECGViewer::keyPressEvent(QKeyEvent* event)
{
    int step = static_cast<int>(0.2 * window_samples_);

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        nudge(-step);
        break;

    case Qt::Key_Right:
    case Qt::Key_D:
        nudge(+step);
        break;

    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (hoverOverlayIndex_ >= 0)
            deleteHoveredOverlay();
        else if (hoverNoteIndex_ >= 0)
            deleteHoveredNote();
        else
            deleteHoveredFiducial();
        break;


    default:
        QMainWindow::keyPressEvent(event);
        break;
    }
}

} // namespace ECGViewer
