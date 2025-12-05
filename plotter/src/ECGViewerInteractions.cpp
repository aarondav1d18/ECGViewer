#include "ECGViewer.hpp"

#include <QMouseEvent>



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

void ECGViewer::onPlotMousePress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (zoomRectMode_) // don't drag labels while in rect zoom mode
        return;

    QCPAbstractItem* item = plot_->itemAt(event->pos(), true);
    if (!item)
        return;

    for (int i = 0; i < fiducialsCurrent_.size(); ++i) {
        auto& f = fiducialsCurrent_[i];
        if (f.line == item || f.text == item) {
            draggingFiducial_ = true;
            activeFiducialIndex_ = i;

            double clickX = plot_->xAxis->pixelToCoord(event->pos().x());
            double currentX = timesFor(f.type)[f.index];  // current fiducial x (seconds)
            dragOffsetSeconds_ = currentX - clickX;

            // Save current interactions and disable range drag while dragging the fiducial
            savedInteractions_ = plot_->interactions();
            plot_->setInteraction(QCP::iRangeDrag, false);

            setCursor(Qt::ClosedHandCursor);
            break;
        }
    }
}

void ECGViewer::onPlotMouseMove(QMouseEvent* event)
{
    // 1) If we are currently dragging a fiducial, do the drag logic
    if (draggingFiducial_ && activeFiducialIndex_ >= 0)
    {
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

        // Keep closed hand while dragging
        setCursor(Qt::ClosedHandCursor);

        plot_->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    // If we're not dragging: do hover feedback (open hand over fiducials)
    if (zoomRectMode_) {
        setCursor(Qt::ArrowCursor);
        hoverFiducialIndex_ = -1;
        return;
    }

    QCPAbstractItem* item = plot_->itemAt(event->pos(), true);
    int foundIndex = -1;

    if (item) {
        for (int i = 0; i < fiducialsCurrent_.size(); ++i) {
            const auto& f = fiducialsCurrent_[i];
            if (f.line == item || f.text == item) {
                foundIndex = i;
                break;
            }
        }
    }

    hoverFiducialIndex_ = foundIndex;

    if (hoverFiducialIndex_ >= 0) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void ECGViewer::onPlotMouseRelease(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (!draggingFiducial_ || activeFiducialIndex_ < 0)
        return;

    auto& f = fiducialsCurrent_[activeFiducialIndex_];

    // Final x position from the line item
    double newTime = f.line->start->coords().x();

    // Update underlying time & value vectors
    QVector<double>& times = timesFor(f.type);
    QVector<double>& vals = valsFor(f.type);

    if (f.index >= 0 && f.index < times.size()) {
        times[f.index] = newTime;

        // Optional: snap Y to underlying clean signal at the nearest sample
        double absTime = t_.first() + newTime; // because we used tRel = t[i] - t0
        int sampleIndex = static_cast<int>(std::round((absTime - t_.first()) * fs_));
        if (sampleIndex < 0)
            sampleIndex = 0;
        if (sampleIndex >= vClean_.size())
            sampleIndex = vClean_.size() - 1;

        vals[f.index] = vClean_[sampleIndex];
    }

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
        deleteHoveredFiducial();
        break;

    default:
        QMainWindow::keyPressEvent(event);
        break;
    }
}
} // namespace ECGViewer