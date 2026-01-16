/**
 * @file ECGViewerPlot.cpp
 * @brief Windowed plotting and item rebuilds for ECGViewer.
 *
 * This translation unit is responsible for drawing and refreshing the visible view:
 * - Updating the displayed ECG window (including downsampling for responsiveness)
 * - Toggling original vs cleaned traces depending on hide_artifacts_
 * - Rebuilding fiducial line/text items for the current x-range
 * - Rebuilding note visuals (point notes vs region rectangles)
 * - Managing window length changes and slider-driven navigation helpers
 *
 * Input events and UI wiring are implemented elsewhere.
 */

#include "ECGViewer.hpp"

#include <algorithm>

namespace ECGViewer {

/**
 * @brief Update the plot to show the window starting at startSample.
 * @details Downsamples to a max point count for responsiveness; draws cleaned signal always,
 * and optionally the original signal when artifacts are not hidden.
 */
void ECGViewer::updateWindow(int startSample)
{
    if (startSample < 0) startSample = 0;
    if (startSample > max_start_sample_) startSample = max_start_sample_;

    const int endSample = std::min(startSample + window_samples_, t_.size());

    const int rawCount = endSample - startSample;
    const int maxPoints = 5000;
    int step = rawCount > maxPoints ? (rawCount / maxPoints) : 1;
    if (step < 1) step = 1;

    QVector<double> txBase, vyBase;
    QVector<double> txNoise, vyNoise;
    QVector<double> txOrigFull, vyOrigFull;

    txBase.reserve(rawCount / step + 1);
    vyBase.reserve(rawCount / step + 1);
    txNoise.reserve(rawCount / step + 1);
    vyNoise.reserve(rawCount / step + 1);
    txOrigFull.reserve(rawCount / step + 1);
    vyOrigFull.reserve(rawCount / step + 1);

    const double t0 = t_.first();

    for (int i = startSample; i < endSample; i += step) {
        const double tRel = t_[i] - t0;
        const double vO   = vOrig_[i];
        const double vC   = vClean_[i];
        const bool isArt  = (artMask_[i] != 0);

        if (!hide_artifacts_) {
            txOrigFull.push_back(tRel);
            vyOrigFull.push_back(vO);
        }

        if (isArt) {
            txNoise.push_back(tRel);
            vyNoise.push_back(vC);
        } else {
            txBase.push_back(tRel);
            vyBase.push_back(vC);
        }
    }

    graphCleanBase_->setData(txBase, vyBase);

    if (!hide_artifacts_) {
        graphOrigFull_->setData(txOrigFull, vyOrigFull);
        graphOrigFull_->setVisible(true);
    } else {
        graphOrigFull_->setVisible(false);
    }

    const double x0 = t_[startSample] - t0;
    const double x1 = x0 + window_s_;
    currentX0 = x0;
    currentX1 = x1;
    plot_->xAxis->setRange(x0, x1);

    updateFiducialLines(x0, x1);
    updateNoteItems(x0, x1);

    plot_->replot();
}

/**
 * @brief Change window length in seconds and refresh the current view.
 * @details Updates derived sample counts and slider bounds, then calls updateWindow.
 */
void ECGViewer::updateWindowLength(double newWindowSeconds)
{
    if (newWindowSeconds < min_window_s_)
        newWindowSeconds = min_window_s_;
    if (newWindowSeconds > total_time_)
        newWindowSeconds = total_time_;

    window_s_ = newWindowSeconds;
    window_samples_ = std::max(1, static_cast<int>(window_s_ * fs_));

    max_start_sample_ = std::max(0, static_cast<int>(t_.size()) - window_samples_ - 1);
    slider_->setMaximum(max_start_sample_);

    int startSample = slider_->value();
    if (startSample > max_start_sample_) {
        startSample = max_start_sample_;
        slider_->setValue(startSample);
    }

    updateWindow(startSample);
}

/**
 * @brief Rebuild fiducial vertical line/text items for the visible x-range.
 * @details Each type contributes zero or more markers. Items are fully recreated each call.
 */
void ECGViewer::updateFiducialLines(double x0, double x1)
{
    for (auto* item : fiducialItems_) {
        plot_->removeItem(item);
    }
    fiducialItems_.clear();
    fiducialsCurrent_.clear();

    auto addLinesFor = [this, x0, x1](const QVector<double>& times,
                                     const QVector<double>& /*vals*/,
                                     FiducialType type,
                                     const QString& label,
                                     const QColor& color)
    {
        for (int i = 0; i < times.size(); ++i) {
            double t = times[i];
            if (t < x0 || t > x1)
                continue;

            auto* line = new QCPItemLine(plot_);
            line->start->setCoords(t, plot_->yAxis->range().lower);
            line->end->setCoords(t, plot_->yAxis->range().upper);
            line->setPen(QPen(color, 0.8, Qt::DashLine));
            line->setSelectable(true);

            auto* txt = new QCPItemText(plot_);
            txt->position->setCoords(t, plot_->yAxis->range().upper);
            txt->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
            txt->setText(QString("%1 @ %2s").arg(label).arg(t, 0, 'f', 5));
            txt->setColor(color);
            txt->setClipToAxisRect(true);
            txt->setRotation(-90);
            txt->setSelectable(true);

            fiducialItems_.push_back(line);
            fiducialItems_.push_back(txt);

            FiducialVisual fv;
            fv.type = type;
            fv.index = i;
            fv.line = line;
            fv.text = txt;
            fiducialsCurrent_.push_back(fv);
        }
    };

    addLinesFor(pTimes_, pVals_, FiducialType::P, "P", Qt::blue);
    addLinesFor(qTimes_, qVals_, FiducialType::Q, "Q", Qt::green);
    addLinesFor(rTimes_, rVals_, FiducialType::R, "R", Qt::red);
    addLinesFor(sTimes_, sVals_, FiducialType::S, "S", Qt::magenta);
    addLinesFor(tTimes_, tVals_, FiducialType::T, "T", QColor(255, 140, 0));
}

/**
 * @brief Rebuild note items for the visible x-range.
 * @details Point notes are drawn as a vertical line + label, region notes as a rect + label.
 */
void ECGViewer::updateNoteItems(double x0, double x1)
{
    for (auto& nv : notesCurrent_) {
        if (nv.line) plot_->removeItem(nv.line);
        if (nv.rect) plot_->removeItem(nv.rect);
        if (nv.text) plot_->removeItem(nv.text);
    }
    notesCurrent_.clear();

    const double yLow  = plot_->yAxis->range().lower;
    const double yHigh = plot_->yAxis->range().upper;

    for (int i = 0; i < notes_.size(); ++i) {
        const Note& n = notes_[i];

        const double t0 = n.time;
        const double t1 = n.time + std::max(0.0, n.duration);

        const bool isRegion = (n.duration > 0.0);
        if (!isRegion) {
            if (t0 < x0 || t0 > x1) continue;
        } else {
            if (t1 < x0 || t0 > x1) continue;
        }

        NoteVisual nv;
        nv.noteIndex = i;

        if (!isRegion) {
            auto* line = new QCPItemLine(plot_);
            line->start->setCoords(t0, yLow);
            line->end->setCoords(t0, yHigh);
            line->setPen(QPen(Qt::darkCyan, 1.0, Qt::DashLine));
            line->setSelectable(true);

            auto* txt = new QCPItemText(plot_);
            txt->position->setCoords(t0, yHigh);
            txt->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
            txt->setText(n.tag.isEmpty() ? QStringLiteral("Note") : n.tag);
            txt->setColor(Qt::darkCyan);
            txt->setBrush(QBrush(QColor(255, 255, 255, 180)));
            txt->setPadding(QMargins(2, 2, 2, 2));
            txt->setClipToAxisRect(true);
            txt->setSelectable(true);

            nv.line = line;
            nv.text = txt;
        } else {
            auto* rect = new QCPItemRect(plot_);
            rect->topLeft->setCoords(t0, yHigh);
            rect->bottomRight->setCoords(t1, yLow);

            QPen pen(Qt::darkCyan);
            pen.setWidthF(1.0);
            rect->setPen(pen);
            rect->setBrush(QBrush(QColor(0, 139, 139, 40)));
            rect->setSelectable(true);
            rect->setClipToAxisRect(true);

            auto* txt = new QCPItemText(plot_);
            txt->position->setCoords(t0, yHigh);
            txt->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
            txt->setText(n.tag.isEmpty() ? QStringLiteral("Region") : n.tag);
            txt->setColor(Qt::darkCyan);
            txt->setBrush(QBrush(QColor(255, 255, 255, 180)));
            txt->setPadding(QMargins(2, 2, 2, 2));
            txt->setClipToAxisRect(true);
            txt->setSelectable(true);

            nv.rect = rect;
            nv.text = txt;
        }

        notesCurrent_.push_back(nv);
    }
}

void ECGViewer::nudge(int deltaSamples)
{
    int newVal = slider_->value() + deltaSamples;
    if (newVal < 0) newVal = 0;
    if (newVal > max_start_sample_) newVal = max_start_sample_;
    slider_->setValue(newVal);
}

} // namespace ECGViewer
