#include "EcgQtViewer.hpp"
#include "qcustomplot.h"

#include <QApplication>
#include <QSlider>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QWidget>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>


// Can move some of the helpers from here to header to reduce amount of code in this file
// or can just leave it or split into multiple files if it gets too big but should be fine while
// working on this class for now.
ECGViewerQt::ECGViewerQt(const QVector<double>& t,
                         const QVector<double>& vOrig,
                         const QVector<double>& vClean,
                         const QVector<unsigned char>& artMask,
                         double fs,
                         double window_s,
                         bool has_ylim,
                         double ymin,
                         double ymax,
                         bool hide_artifacts,
                         const QVector<double>& pTimes,
                         const QVector<double>& pVals,
                         const QVector<double>& qTimes,
                         const QVector<double>& qVals,
                         const QVector<double>& rTimes,
                         const QVector<double>& rVals,
                         const QVector<double>& sTimes,
                         const QVector<double>& sVals,
                         const QVector<double>& tTimes,
                         const QVector<double>& tVals,
                         QWidget* parent)
    : QMainWindow(parent),
      t_(t),
      vOrig_(vOrig),
      vClean_(vClean),
      artMask_(artMask),
      pTimes_(pTimes), pVals_(pVals),
      qTimes_(qTimes), qVals_(qVals),
      rTimes_(rTimes), rVals_(rVals),
      sTimes_(sTimes), sVals_(sVals),
      tTimes_(tTimes), tVals_(tVals),
      fs_(fs),
      window_s_(window_s),
      hide_artifacts_(hide_artifacts)
{
    if (t_.size() != vOrig_.size() ||
        t_.size() != vClean_.size() ||
        t_.size() != artMask_.size() ||
        t_.isEmpty()) {
        throw std::runtime_error("All input vectors must be non-empty and of equal length");
    }

    // Time/window bookkeeping 
    total_time_ = t_.last() - t_.first();
    if (total_time_ <= 0.0) {
        total_time_ = 1.0 / std::max(fs_, 1.0); // avoid degenerate case
    }

    if (window_s_ <= 0.0 || window_s_ > total_time_) {
        window_s_ = total_time_;
    }

    window_s_original_ = window_s_;
    // min window: at least ~50ms or ~5 samples, whichever is larger
    min_window_s_ = std::max(0.05, 5.0 / std::max(fs_, 1.0));

    window_samples_ = static_cast<int>(window_s_ * fs_);
    if (window_samples_ <= 0)
        window_samples_ = std::min(t_.size(), 1);

    max_start_sample_ = std::max(0, t_.size() - window_samples_ - 1);

    // UI setup 
    auto* central = new QWidget(this);
    auto* vbox = new QVBoxLayout(central);

    plot_ = new QCustomPlot(central);
    vbox->addWidget(plot_, 1);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectItems);

    connect(plot_, &QCustomPlot::mousePress,  this, &ECGViewerQt::onPlotMousePress);
    connect(plot_, &QCustomPlot::mouseMove,   this, &ECGViewerQt::onPlotMouseMove);
    connect(plot_, &QCustomPlot::mouseRelease,this, &ECGViewerQt::onPlotMouseRelease);

    // Axes labels
    plot_->xAxis->setLabel("Time (s)");
    plot_->yAxis->setLabel("Voltage (V)");
    plot_->xAxis->grid()->setVisible(true);
    plot_->yAxis->grid()->setVisible(true);

    // Default: normal drag/zoom via wheel
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot_->axisRect()->setRangeDrag(Qt::Horizontal);
    plot_->axisRect()->setRangeZoom(Qt::Horizontal);
    plot_->axisRect()->setRangeZoomAxes(plot_->xAxis, plot_->yAxis);


    // Setup selection rect appearance (used in rect zoom mode)
    plot_->selectionRect()->setPen(QPen(Qt::red));
    plot_->selectionRect()->setBrush(QBrush(QColor(255, 0, 0, 50)));


    if (has_ylim) {
        plot_->yAxis->setRange(ymin, ymax);
    } else {
        plot_->yAxis->setRange(-0.1, 0.15);
    }

    // Store original y-axis range for "Reset View"
    y_min_orig_ = plot_->yAxis->range().lower;
    y_max_orig_ = plot_->yAxis->range().upper;

    // Graphs 
    // cleaned base (non-artifact)
    graphCleanBase_ = plot_->addGraph();
    graphCleanBase_->setPen(QPen(Qt::blue, 1.2));

    // full original trace (thin grey)
    graphOrigFull_ = plot_->addGraph();
    {
        QPen p(Qt::gray);
        p.setWidthF(0.8);
        p.setStyle(Qt::SolidLine);
        graphOrigFull_->setPen(p);
    }

    // Scatter graphs for fiducials
    auto makeScatterGraph = [this](const QColor& color, QCPScatterStyle::ScatterShape shape, 
                                   double size) -> QCPGraph* {
        QCPGraph* g = plot_->addGraph();
        g->setLineStyle(QCPGraph::lsNone);
        g->setScatterStyle(QCPScatterStyle(shape, size));
        g->setPen(QPen(color));
        return g;
    };

    graphP_ = makeScatterGraph(Qt::blue, QCPScatterStyle::ssDisc, 6);
    graphQ_ = makeScatterGraph(Qt::green, QCPScatterStyle::ssDisc, 6);
    graphR_ = makeScatterGraph(Qt::red, QCPScatterStyle::ssTriangle, 8);
    graphS_ = makeScatterGraph(Qt::magenta,QCPScatterStyle::ssDisc, 6);
    graphT_ = makeScatterGraph(QColor(255, 140, 0), QCPScatterStyle::ssDisc, 6); // orange-ish

    // Set full fiducial scatter data (QCustomPlot will clip to window)
    graphP_->setData(pTimes_, pVals_);
    graphQ_->setData(qTimes_, qVals_);
    graphR_->setData(rTimes_, rVals_);
    graphS_->setData(sTimes_, sVals_);
    graphT_->setData(tTimes_, tVals_);

    // Tabs at the bottom: Traversal + Manual Insert
    tabWidget_ = new QTabWidget(central);

    // Traversal tab (existing controls)
    QWidget* traversalTab = new QWidget(tabWidget_);
    auto* traversalLayout = new QHBoxLayout(traversalTab);

    btnLeft_ = new QPushButton("Left", traversalTab);
    btnRight_ = new QPushButton("Right", traversalTab);
    btnZoomIn_ = new QPushButton("Zoom In", traversalTab);
    btnZoomOut_ = new QPushButton("Zoom Out", traversalTab);
    btnResetView_ = new QPushButton("Reset View", traversalTab);
    btnExit_ = new QPushButton("Exit", traversalTab);
    btnZoomRect_ = new QPushButton("Rect Zoom", traversalTab);
    btnZoomRect_->setCheckable(true);

    slider_ = new QSlider(Qt::Horizontal, traversalTab);
    slider_->setMinimum(0);
    slider_->setMaximum(max_start_sample_);
    slider_->setSingleStep(1);

    traversalLayout->addWidget(btnLeft_);
    traversalLayout->addWidget(btnRight_);
    traversalLayout->addWidget(btnZoomIn_);
    traversalLayout->addWidget(btnZoomOut_);
    traversalLayout->addWidget(btnResetView_);
    traversalLayout->addWidget(btnExit_);
    traversalLayout->addWidget(btnZoomRect_);
    traversalLayout->addWidget(slider_);

    traversalTab->setLayout(traversalLayout);
    tabWidget_->addTab(traversalTab, "Traversal");

    // Manual insert tab
    QWidget* manualTab = new QWidget(tabWidget_);
    auto* manualLayout = new QHBoxLayout(manualTab);

    auto* typeLabel = new QLabel("Fiducial type:", manualTab);
    manualTypeCombo_ = new QComboBox(manualTab);
    manualTypeCombo_->addItem("P");
    manualTypeCombo_->addItem("Q");
    manualTypeCombo_->addItem("R");
    manualTypeCombo_->addItem("S");
    manualTypeCombo_->addItem("T");

    manualInsertButton_ = new QPushButton("Insert at centre", manualTab);

    manualLayout->addWidget(typeLabel);
    manualLayout->addWidget(manualTypeCombo_);
    manualLayout->addWidget(manualInsertButton_);
    manualLayout->addStretch(1);

    manualTab->setLayout(manualLayout);
    tabWidget_->addTab(manualTab, "Manual keypoints");

    // Add the tab widget to bottom of main layout
    vbox->addWidget(tabWidget_);


    setCentralWidget(central);
    setWindowTitle("ECG Viewer (Qt)");

    // Connections 
    connect(slider_, &QSlider::valueChanged,
            this, [this](int value) { updateWindow(value); });

    auto buttonStep = [this]() {
        return static_cast<int>(0.2 * window_samples_);
    };
    connect(manualInsertButton_, &QPushButton::clicked,
        this, &ECGViewerQt::onInsertManualFiducial);

    connect(btnZoomRect_, &QPushButton::toggled,
            this, [this](bool checked)
    {
        zoomRectMode_ = checked;
        if (zoomRectMode_) {
            // enable rectangle zoom; wheel zoom still works
            plot_->setInteractions(QCP::iRangeZoom);
            plot_->setSelectionRectMode(QCP::srmZoom);
        } else {
            plot_->setSelectionRectMode(QCP::srmNone);
            plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
        }
    });
    // Stop user from dragging window to negative x and also allow use to drag instead of using
    // arrow keys or slider
    connect(plot_->xAxis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &newRange)
    {
        // Avoid recursion or interference while dragging fiducials
        if (suppressRangeHandler_ || draggingFiducial_)
            return;

        double xLower = newRange.lower;
        double xUpper = newRange.upper;
        double width  = newRange.size();

        const double minLower = 0.0;
        const double maxUpper = total_time_;

        if (xLower < minLower) {
            xLower = minLower;
            xUpper = xLower + width;
        }

        if (xUpper > maxUpper) {
            xUpper = maxUpper;
            xLower = xUpper - width;
        }

        suppressRangeHandler_ = true;
        plot_->xAxis->setRange(xLower, xUpper);
        suppressRangeHandler_ = false;

        double clampedLower = xLower;
        double maxLower = std::max(0.0, total_time_ - window_s_);

        if (clampedLower > maxLower)
            clampedLower = maxLower;

        int startSample = static_cast<int>(clampedLower * fs_);
        if (startSample < 0)
            startSample = 0;
        if (startSample > max_start_sample_)
            startSample = max_start_sample_;

        suppressRangeHandler_ = true;
        slider_->setValue(startSample);   // triggers updateWindow(...)
        suppressRangeHandler_ = false;
    });


    connect(btnLeft_, &QPushButton::clicked,
            this, [this, buttonStep]() {
                nudge(-buttonStep());
            });

    connect(btnRight_, &QPushButton::clicked,
            this, [this, buttonStep]() {
                nudge(+buttonStep());
            });

    // Zoom in/out buttons (time zoom on x-axis)
    connect(btnZoomIn_, &QPushButton::clicked,
            this, [this]() {
                updateWindowLength(window_s_ / 1.5);  // zoom in
            });

    connect(btnZoomOut_, &QPushButton::clicked,
            this, [this]() {
                updateWindowLength(window_s_ * 1.5);  // zoom out
            });

    // Reset view (time + y-range)
    connect(btnResetView_, &QPushButton::clicked,
            this, [this]() {
                updateWindowLength(window_s_original_);
                plot_->yAxis->setRange(y_min_orig_, y_max_orig_);
                plot_->replot();
            });

    // Exit button
    connect(btnExit_, &QPushButton::clicked,
            this, [this]() {
                close();
            });


    // Initial window
    updateWindow(0);
}

QVector<double>& ECGViewerQt::timesFor(FiducialType type)
{
    switch (type) {
    case FiducialType::P: return pTimes_;
    case FiducialType::Q: return qTimes_;
    case FiducialType::R: return rTimes_;
    case FiducialType::S: return sTimes_;
    case FiducialType::T: return tTimes_;
    }
    // just to silence compiler, should never hit:
    return pTimes_;
}

QVector<double>& ECGViewerQt::valsFor(FiducialType type)
{
    switch (type) {
    case FiducialType::P: return pVals_;
    case FiducialType::Q: return qVals_;
    case FiducialType::R: return rVals_;
    case FiducialType::S: return sVals_;
    case FiducialType::T: return tVals_;
    }
    return pVals_;
}

void ECGViewerQt::onInsertManualFiducial()
{
    QString choice = manualTypeCombo_ ? manualTypeCombo_->currentText() : QString("R");

    FiducialType type = FiducialType::R;
    if (choice == "P")      type = FiducialType::P;
    else if (choice == "Q") type = FiducialType::Q;
    else if (choice == "R") type = FiducialType::R;
    else if (choice == "S") type = FiducialType::S;
    else if (choice == "T") type = FiducialType::T;

    // Insert at the centre of the current window
    double newTime = 0.5 * (currentX0 + currentX1);

    // Clamp to full duration just in case
    if (newTime < 0.0) newTime = 0.0;
    if (newTime > total_time_) newTime = total_time_;

    // Get Y value from clean signal at nearest sample
    double absTime = t_.first() + newTime;
    int sampleIndex = static_cast<int>(std::round((absTime - t_.first()) * fs_));
    if (sampleIndex < 0) sampleIndex = 0;
    if (sampleIndex >= vClean_.size()) sampleIndex = vClean_.size() - 1;
    double newVal = vClean_[sampleIndex];

    // Insert into correct vectors, keeping them sorted by time
    QVector<double>& times = timesFor(type);
    QVector<double>& vals = valsFor(type);

    int insertIndex = 0;
    while (insertIndex < times.size() && times[insertIndex] < newTime)
        ++insertIndex;

    times.insert(insertIndex, newTime);
    vals.insert(insertIndex, newVal);

    // Update the correct scatter graph
    switch (type) {
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

    // Recreate fiducial lines/labels for the current window
    updateFiducialLines(currentX0, currentX1);

    // Replot so the new fiducial appears (and is draggable using your existing logic)
    plot_->replot();
}


/// @brief Nudges the current window by a specified number of samples
/// @param deltaSamples Number of samples to nudge (positive or negative)
void ECGViewerQt::nudge(int deltaSamples) {
    int newVal = slider_->value() + deltaSamples;
    if (newVal < 0) newVal = 0;
    if (newVal > max_start_sample_) newVal = max_start_sample_;
    slider_->setValue(newVal); // triggers updateWindow
}

/**
 * @brief Updates the displayed ECG window based on the starting sample index.
 * @details This function updates the ECG plot to show a specific window of data
 * starting from the given sample index. It handles downsampling for performance,
 * and separates the data into clean and artifact segments for visualization.
 * 
 * @param startSample The starting sample index for the window to display.
 * @return void
 */
void ECGViewerQt::updateWindow(int startSample) {
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

    plot_->replot();
}

/// @brief Updates the window length for the ECG viewer
/// @param newWindowSeconds The new window length in seconds
void ECGViewerQt::updateWindowLength(double newWindowSeconds) {
    if (newWindowSeconds < min_window_s_)
        newWindowSeconds = min_window_s_;
    if (newWindowSeconds > total_time_)
        newWindowSeconds = total_time_;

    window_s_ = newWindowSeconds;
    window_samples_ = std::max(1, static_cast<int>(window_s_ * fs_));

    max_start_sample_ = std::max(0, t_.size() - window_samples_ - 1);
    slider_->setMaximum(max_start_sample_);

    int startSample = slider_->value();
    if (startSample > max_start_sample_) {
        startSample = max_start_sample_;
        slider_->setValue(startSample);
    }

    updateWindow(startSample);
}

/// @brief Updates fiducial lines on the plot within the specified x-axis range
void ECGViewerQt::updateFiducialLines(double x0, double x1) {
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

void ECGViewerQt::deleteHoveredFiducial()
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


void ECGViewerQt::onPlotMousePress(QMouseEvent* event)
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


void ECGViewerQt::onPlotMouseMove(QMouseEvent* event)
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



void ECGViewerQt::onPlotMouseRelease(QMouseEvent* event)
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
void ECGViewerQt::keyPressEvent(QKeyEvent* event) {
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


