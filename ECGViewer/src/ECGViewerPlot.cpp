#include "ECGViewer.hpp"

#include <algorithm> // for std::min, std::max

namespace ECGViewer {

/**
 * @brief Updates the displayed ECG window based on the starting sample index.
 * @details This function updates the ECG plot to show a specific window of data
 * starting from the given sample index. It handles downsampling for performance,
 * and separates the data into clean and artifact segments for visualization.
 * 
 * @param startSample The starting sample index for the window to display.
 * @return void
 */
void ECGViewer::updateWindow(int startSample) {
    if (startSample < 0) startSample = 0;
    if (startSample > max_start_sample_) startSample = max_start_sample_;

    const int endSample = std::min(startSample + window_samples_, t_.size());

    // downsample to at most ~5000 points for performance
    const int rawCount = endSample - startSample;
    const int maxPoints = 5000;
    int step = rawCount > maxPoints ? (rawCount / maxPoints) : 1;
    if (step < 1) step = 1;

    // Cleaned signal (visual replacement) split into base vs noise segments
    QVector<double> txBase, vyBase;
    QVector<double> txNoise, vyNoise;

    // Original trace (with artefacts) – only used if hide_artifacts_ == false
    QVector<double> txOrigFull, vyOrigFull;

    // (You can drop these if you don't need original-artifact-only data)
    // QVector<double> txOrigArt, vyOrigArt;

    txBase.reserve(rawCount / step + 1);
    vyBase.reserve(rawCount / step + 1);
    txNoise.reserve(rawCount / step + 1);
    vyNoise.reserve(rawCount / step + 1);
    txOrigFull.reserve(rawCount / step + 1);
    vyOrigFull.reserve(rawCount / step + 1);
    // txOrigArt.reserve(rawCount / step + 1);
    // vyOrigArt.reserve(rawCount / step + 1);

    const double t0 = t_.first(); // make time relative like Python

    for (int i = startSample; i < endSample; i += step) {
        const double tRel = t_[i] - t0;
        const double vO   = vOrig_[i];
        const double vC   = vClean_[i];
        const bool isArt  = (artMask_[i] != 0);

        // Original full trace (with artefacts) – only if we're showing artefacts
        if (!hide_artifacts_) {
            txOrigFull.push_back(tRel);
            vyOrigFull.push_back(vO);
        }

        // Cleaned signal: split into base vs noise-replacement segments
        if (isArt) {
            // This is the visual replacement for artefacted samples
            txNoise.push_back(tRel);
            vyNoise.push_back(vC);

            // original-artifact-only:
            // txOrigArt.push_back(tRel);
            // vyOrigArt.push_back(vO);
        } else {
            // Cleaned signal on non-artifact samples
            txBase.push_back(tRel);
            vyBase.push_back(vC);
        }
    }

    // Always show the cleaned signal (visual fix)
    graphCleanBase_->setData(txBase,  vyBase);

    // Show or hide the original noisy ECG depending on hide_artifacts_
    if (!hide_artifacts_) {
        graphOrigFull_->setData(txOrigFull, vyOrigFull);
        graphOrigFull_->setVisible(true);
    } else {
        graphOrigFull_->setVisible(false);
        // graphOrigFull_->setData(QVector<double>(), QVector<double>());
    }

    // X-axis limits: window_s_ seconds from startSample
    const double x0 = t_[startSample] - t0;
    const double x1 = x0 + window_s_;
    currentX0 = x0;
    currentX1 = x1;
    plot_->xAxis->setRange(x0, x1);

    updateFiducialLines(x0, x1);
    updateNoteItems(x0, x1);

    plot_->replot();
}

/// @brief Updates the window length for the ECG viewer
/// @param newWindowSeconds The new window length in seconds
void ECGViewer::updateWindowLength(double newWindowSeconds) {
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

/// @brief Updates fiducial lines on the plot within the specified x-axis range
void ECGViewer::updateFiducialLines(double x0, double x1) {
    // remove old items
    for (auto* item : fiducialItems_) {
        plot_->removeItem(item);
    }
    fiducialItems_.clear();
    fiducialsCurrent_.clear();

    auto addLinesFor = [this, x0, x1](const QVector<double>& times,
                                      const QVector<double>& vals,
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

void ECGViewer::updateNoteItems(double x0, double x1)
{
    // Remove old note items
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

        // Visibility test:
        // - point note visible if t0 in [x0,x1]
        // - region visible if it overlaps [x0,x1]
        const bool isRegion = (n.duration > 0.0);
        if (!isRegion) {
            if (t0 < x0 || t0 > x1) continue;
        } else {
            if (t1 < x0 || t0 > x1) continue;
        }

        NoteVisual nv;
        nv.noteIndex = i;

        if (!isRegion) {
            // point note (existing behavior)
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
            // region note
            auto* rect = new QCPItemRect(plot_);
            rect->topLeft->setCoords(t0, yHigh);
            rect->bottomRight->setCoords(t1, yLow);

            QPen pen(Qt::darkCyan);
            pen.setWidthF(1.0);
            rect->setPen(pen);
            rect->setBrush(QBrush(QColor(0, 139, 139, 40))); // translucent fill
            rect->setSelectable(true);
            rect->setClipToAxisRect(true);

            // label at start (you can use mid if you prefer)
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


/// @brief Nudges the current window by a specified number of samples
/// @param deltaSamples Number of samples to nudge (positive or negative)
void ECGViewer::nudge(int deltaSamples) {
    int newVal = slider_->value() + deltaSamples;
    if (newVal < 0) newVal = 0;
    if (newVal > max_start_sample_) newVal = max_start_sample_;
    slider_->setValue(newVal); // triggers updateWindow
}

} // namespace ECGViewer