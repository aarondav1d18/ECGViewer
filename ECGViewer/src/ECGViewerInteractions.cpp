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

    if (!item)
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
}

/**
 * @brief Mouse move updates active drags or provides hover feedback.
 * @details Updates plot items in-place during drags for responsiveness, then queues replot.
 */
void ECGViewer::onPlotMouseMove(QMouseEvent* event)
{
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

    if (item) {
        for (int i = 0; i < notesCurrent_.size(); ++i) {
            const auto& nv = notesCurrent_[i];
            if (nv.line == item || nv.text == item || nv.rect == item) {
                foundNoteIndex = i;
                break;
            }
        }

        if (foundNoteIndex < 0) {
            for (int i = 0; i < fiducialsCurrent_.size(); ++i) {
                const auto& f = fiducialsCurrent_[i];
                if (f.line == item || f.text == item) {
                    foundFidIndex = i;
                    break;
                }
            }
        }
    }

    hoverNoteIndex_ = foundNoteIndex;
    hoverFiducialIndex_ = foundFidIndex;

    if (hoverNoteIndex_ >= 0 || hoverFiducialIndex_ >= 0) {
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
        if (hoverNoteIndex_ >= 0)
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
