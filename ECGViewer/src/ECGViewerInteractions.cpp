#include "ECGViewer.hpp"

#include <QMouseEvent>
#include <algorithm>



namespace ECGViewer {

void ECGViewer::deleteHoveredFiducial()
{
    if (hoverFiducialIndex_ < 0 || hoverFiducialIndex_ >= fiducialsCurrent_.size())
        return;

    const auto f = fiducialsCurrent_[hoverFiducialIndex_];

    // Remove from underlying time/value vectors
    QVector<double>& times = timesFor(f.type);
    QVector<double>& vals  = valsFor(f.type);

    if (f.index < 0 || f.index >= times.size())
        return;

    times.remove(f.index);
    vals.remove(f.index);

    // Update the appropriate scatter graph
    switch (f.type) {
    case FiducialType::P:
        graphP_->setData(pTimes_, pVals_);
        break;
    case FiducialType::Q:
        graphQ_->setData(qTimes_, qVals_);
        break;
    case FiducialType::R:
        graphR_->setData(rTimes_, rVals_);
        break;
    case FiducialType::S:
        graphS_->setData(sTimes_, sVals_);
        break;
    case FiducialType::T:
        graphT_->setData(tTimes_, tVals_);
        break;
    }

    // Recreate fiducial lines/labels for current window
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

    // Find note under cursor
    for (int i = 0; i < notesCurrent_.size(); ++i) {
        const auto& nv = notesCurrent_[i];
        if (nv.text == item || nv.line == item || nv.rect == item) {
            openNoteEditor(nv.noteIndex);
            return;
        }
    }
}


void ECGViewer::onPlotMousePress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (zoomRectMode_) // don't drag labels while in rect zoom mode
        return;

    const bool shiftHeld = (event->modifiers() & Qt::ShiftModifier) != 0;

    // If shift is held and we're not clicking an existing selectable item,
    // start drawing a region note.
    QCPAbstractItem* item = plot_->itemAt(event->pos(), true);
    auto mouseTime = [this, event]() {
        double x = plot_->xAxis->pixelToCoord(event->pos().x());
        return std::clamp(x, 0.0, total_time_);
    };
    if (shiftHeld && item) {
        for (int i = 0; i < notesCurrent_.size(); ++i) {
            auto& nv = notesCurrent_[i];
            if (!(nv.line == item || nv.text == item || nv.rect == item))
                continue;

            // Only regions are resizable
            if (nv.noteIndex < 0 || nv.noteIndex >= notes_.size())
                return;

            Note& n = notes_[nv.noteIndex];
            if (n.duration <= 0.0) {
                // For point notes, Shift+click = edit (optional behavior)
                openNoteEditor(nv.noteIndex);
                return;
            }

            // Region note: decide resize-left / resize-right / move
            const double t0 = n.time;
            const double t1 = n.time + n.duration;

            const int px = event->pos().x();
            const int leftPx  = plot_->xAxis->coordToPixel(t0);
            const int rightPx = plot_->xAxis->coordToPixel(t1);

            const int edgeTolPx = 7; // tweak feel: 5-10 px

            noteDragMode_ = NoteDragMode::Move;
            if (std::abs(px - leftPx) <= edgeTolPx)
                noteDragMode_ = NoteDragMode::ResizeLeft;
            else if (std::abs(px - rightPx) <= edgeTolPx)
                noteDragMode_ = NoteDragMode::ResizeRight;

            draggingNote_ = true;
            activeNoteVisualIndex_ = i;

            regionPressTime_ = mouseTime();
            originalStart_ = t0;
            originalEnd_ = t1;

            // For move mode, keep same offset behavior you already use
            if (noteDragMode_ == NoteDragMode::Move) {
                noteDragOffsetSeconds_ = n.time - plot_->xAxis->pixelToCoord(event->pos().x());
            } else {
                noteDragOffsetSeconds_ = 0.0;
            }

            savedInteractions_ = plot_->interactions();
            plot_->setInteraction(QCP::iRangeDrag, false);
            setCursor(Qt::SizeHorCursor); // horizontal resize-ish cursor
            return;
        }
    }
    if (shiftHeld && !item) {
        double clickX = plot_->xAxis->pixelToCoord(event->pos().x());
        clickX = std::clamp(clickX, 0.0, total_time_);

        creatingRegion_ = true;
        regionAnchorTime_ = clickX;

        // Create a new region note (duration starts at 0)
        Note n;
        n.time = clickX;
        const double eps = 1.0 / std::max(fs_, 1.0);
        n.duration = eps; // NOT 0.0
        n.tag = QStringLiteral("Region %1").arg(notes_.size() + 1);
        n.detail = QString();

        // volts optional: sample at start
        {
            double absTime = t_.first() + n.time;
            int sampleIndex = static_cast<int>(std::round((absTime - t_.first()) * fs_));
            sampleIndex = std::clamp(sampleIndex, 0, vClean_.size() - 1);
            n.volts = vClean_[sampleIndex];
        }

        notes_.push_back(n);
        creatingNoteIndex_ = notes_.size() - 1;

        // Build visuals and find the visual index for this note
        updateNoteItems(currentX0, currentX1);
        for (int i = 0; i < notesCurrent_.size(); ++i) {
            if (notesCurrent_[i].noteIndex == creatingNoteIndex_) {
                activeNoteVisualIndex_ = i; // reuse this so we can update visuals in-place
                break;
            }
        }

        savedInteractions_ = plot_->interactions();
        plot_->setInteraction(QCP::iRangeDrag, false);
        setCursor(Qt::CrossCursor);

        plot_->replot(QCustomPlot::rpQueuedReplot);
        noteDragMode_ = NoteDragMode::CreateRegion;
        return;
    }


    if (!item)
        return;

    // Check notes first
    for (int i = 0; i < notesCurrent_.size(); ++i) {
        auto& nv = notesCurrent_[i];
        if (nv.line == item || nv.text == item || nv.rect == item) {
            draggingNote_ = true;
            activeNoteVisualIndex_ = i;
            noteDragMode_ = NoteDragMode::Move;

            double clickX = plot_->xAxis->pixelToCoord(event->pos().x());
            const Note& note = notes_[nv.noteIndex];
            noteDragOffsetSeconds_ = note.time - clickX;

            savedInteractions_ = plot_->interactions();
            plot_->setInteraction(QCP::iRangeDrag, false);
            setCursor(Qt::ClosedHandCursor);
            return;
        }
    }

    // Fallback to fiducials
    for (int i = 0; i < fiducialsCurrent_.size(); ++i) {
        auto& f = fiducialsCurrent_[i];
        if (f.line == item || f.text == item) {
            draggingFiducial_ = true;
            activeFiducialIndex_ = i;

            double clickX = plot_->xAxis->pixelToCoord(event->pos().x());
            double currentX = timesFor(f.type)[f.index];  // current fiducial x (seconds)
            dragOffsetSeconds_ = currentX - clickX;

            savedInteractions_ = plot_->interactions();
            plot_->setInteraction(QCP::iRangeDrag, false);

            setCursor(Qt::ClosedHandCursor);
            return;
        }
    }
}


void ECGViewer::onPlotMouseMove(QMouseEvent* event)
{
    if (creatingRegion_ && creatingNoteIndex_ >= 0 && creatingNoteIndex_ < notes_.size())
    {
        Note& n = notes_[creatingNoteIndex_];

        auto clampTime = [this](double x) { return std::clamp(x, 0.0, total_time_); };
        double mouseX = clampTime(plot_->xAxis->pixelToCoord(event->pos().x()));

        const double eps = 1.0 / std::max(fs_, 1.0);

        double t0 = std::min(regionAnchorTime_, mouseX);
        double t1 = std::max(regionAnchorTime_, mouseX);

        n.time = t0;
        n.duration = std::max(eps, t1 - t0);

        // Update the visual if we have it, otherwise rebuild and re-find it
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


    // If we are currently dragging a note, handle that first
    if (draggingNote_ && activeNoteVisualIndex_ >= 0)    {
        if (activeNoteVisualIndex_ >= notesCurrent_.size())
            return;

        auto& nv = notesCurrent_[activeNoteVisualIndex_];
        if (nv.noteIndex < 0 || nv.noteIndex >= notes_.size())
            return;

        Note& n = notes_[nv.noteIndex];

        auto clampTime = [this](double x) {
            return std::clamp(x, 0.0, total_time_);
        };

        double mouseX = clampTime(plot_->xAxis->pixelToCoord(event->pos().x()));
        const double eps = 1.0 / std::max(fs_, 1.0);

        // create region
        if (noteDragMode_ == NoteDragMode::CreateRegion)
        {
            double t0 = std::min(regionAnchorTime_, mouseX);
            double t1 = std::max(regionAnchorTime_, mouseX);

            n.time = t0;
            n.duration = std::max(eps, t1 - t0);
        }

        // resize lefft edge
        else if (noteDragMode_ == NoteDragMode::ResizeLeft)
        {
            double newStart = clampTime(std::min(mouseX, originalEnd_));
            double newEnd   = originalEnd_;

            if (newEnd - newStart < eps)
                newStart = newEnd - eps;

            n.time = newStart;
            n.duration = std::max(eps, newEnd - newStart);
        }

        // resize right edge
        else if (noteDragMode_ == NoteDragMode::ResizeRight)
        {
            double newStart = originalStart_;
            double newEnd   = clampTime(std::max(mouseX, originalStart_));

            if (newEnd - newStart < eps)
                newEnd = newStart + eps;

            n.time = newStart;
            n.duration = std::max(eps, newEnd - newStart);
        }

        // move note (region or point)
        else // NoteDragMode::Move
        {
            double newStart = clampTime(mouseX + noteDragOffsetSeconds_);

            if (n.duration > 0.0) {
                // keep region fully inside signal
                if (newStart + n.duration > total_time_)
                    newStart = std::max(0.0, total_time_ - n.duration);
            }

            n.time = newStart;
        }

        // update visuals in place
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


    // If we are currently dragging a fiducial, do that logic
    if (draggingFiducial_ && activeFiducialIndex_ >= 0)
    {
        if (activeFiducialIndex_ >= fiducialsCurrent_.size())
            return;

        auto& f = fiducialsCurrent_[activeFiducialIndex_];

        double mouseX = plot_->xAxis->pixelToCoord(event->pos().x());
        double newTime = mouseX + dragOffsetSeconds_;

        // Clamp to full signal duration [0, total_time_]
        if (newTime < 0.0)
            newTime = 0.0;
        else if (newTime > total_time_)
            newTime = total_time_;

        double yLow  = plot_->yAxis->range().lower;
        double yHigh = plot_->yAxis->range().upper;

        f.line->start->setCoords(newTime, yLow);
        f.line->end->setCoords(newTime, yHigh);
        f.text->position->setCoords(newTime, yHigh);

        QString label;
        switch (f.type) {
        case FiducialType::P: label = "P"; break;
        case FiducialType::Q: label = "Q"; break;
        case FiducialType::R: label = "R"; break;
        case FiducialType::S: label = "S"; break;
        case FiducialType::T: label = "T"; break;
        }

        f.text->setText(QString("%1 @ %2s").arg(label).arg(newTime, 0, 'f', 5));

        setCursor(Qt::ClosedHandCursor);

        updatePoint(f, newTime);

        // Refresh scatter graphs so points move too
        graphP_->setData(pTimes_, pVals_);
        graphQ_->setData(qTimes_, qVals_);
        graphR_->setData(rTimes_, rVals_);
        graphS_->setData(sTimes_, sVals_);
        graphT_->setData(tTimes_, tVals_);

        plot_->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    // If we're not dragging: hover feedback
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
        // Check notes first (line/text/rect)
        for (int i = 0; i < notesCurrent_.size(); ++i) {
            const auto& nv = notesCurrent_[i];
            if (nv.line == item || nv.text == item || nv.rect == item) {
                foundNoteIndex = i;
                break;
            }
        }

        // If no note was hit, check fiducials
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


void ECGViewer::updatePoint(FiducialVisual& f, double newTime) {
    // Update underlying time & value vectors
    QVector<double>& times = timesFor(f.type);
    QVector<double>& vals = valsFor(f.type);

    if (f.index >= 0 && f.index < times.size()) {
        times[f.index] = newTime;

        double absTime = t_.first() + newTime; // because we used tRel = t[i] - t0
        int sampleIndex = static_cast<int>(std::round((absTime - t_.first()) * fs_));
        if (sampleIndex < 0)
            sampleIndex = 0;
        if (sampleIndex >= vClean_.size())
            sampleIndex = vClean_.size() - 1;

        vals[f.index] = vClean_[sampleIndex];
    }
}

void ECGViewer::onPlotMouseRelease(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (creatingRegion_) {
        creatingRegion_ = false;

        // If the user just clicked without dragging, duration may be tiny.
        // You can either keep it as a point note or delete it.
        if (creatingNoteIndex_ >= 0 && creatingNoteIndex_ < notes_.size()) {
            Note& n = notes_[creatingNoteIndex_];

            // Optional: treat tiny regions as point notes
            const double minDur = 1.0 / std::max(fs_, 1.0); // ~1 sample
            if (n.duration < minDur) {
                n.duration = 0.0;
            }

            // Clamp end
            if (n.time < 0.0) n.time = 0.0;
            if (n.time > total_time_) n.time = total_time_;
            if (n.duration < 0.0) n.duration = 0.0;
            if (n.time + n.duration > total_time_)
                n.duration = std::max(0.0, total_time_ - n.time);

            // Rebuild visuals (because a point note may need a line instead of rect)
            updateNoteItems(currentX0, currentX1);
            refreshNotesList();
            plot_->replot();

            // Optional: open editor after creation
            openNoteEditor(creatingNoteIndex_);
        }

        creatingNoteIndex_ = -1;
        activeNoteVisualIndex_ = -1;
        regionAnchorTime_ = 0.0;

        setCursor(Qt::ArrowCursor);
        plot_->setInteractions(savedInteractions_);
        noteDragMode_ = NoteDragMode::None;
        return;
    }


        // Note drag end
    if (draggingNote_ && activeNoteVisualIndex_ >= 0) {
        draggingNote_ = false;
        activeNoteVisualIndex_ = -1;
        noteDragOffsetSeconds_ = 0.0;

        setCursor(Qt::ArrowCursor);
        plot_->setInteractions(savedInteractions_);
        plot_->replot();
        // no need to recreate items – we directly updated them
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

    // Refresh scatter graphs so points move too
    graphP_->setData(pTimes_, pVals_);
    graphQ_->setData(qTimes_, qVals_);
    graphR_->setData(rTimes_, rVals_);
    graphS_->setData(sTimes_, sVals_);
    graphT_->setData(tTimes_, tVals_);

    // Reset drag state and cursor
    draggingFiducial_ = false;
    activeFiducialIndex_ = -1;
    dragOffsetSeconds_ = 0.0;
    setCursor(Qt::ArrowCursor);

    // Restore original interactions (re-enable range drag, etc.)
    plot_->setInteractions(savedInteractions_);

    plot_->replot();
}

/// @brief Handles key press events for nudge functionality
void ECGViewer::keyPressEvent(QKeyEvent* event) {
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
