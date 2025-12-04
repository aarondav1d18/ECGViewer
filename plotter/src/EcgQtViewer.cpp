#include "EcgQtViewer.hpp"
#include "qcustomplot.h"

#include <QApplication>
#include <QSlider>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QWidget>
// TODO: Remove milivolts option and just expect volts everywhere

// TODO: Edit the drag thing so that it either cant go beyond data limits (no empty space)
// or update with blank space when dragged beyond data limits
// TODO: Make pybindings for ECGConfig instead of importing from Python

ECGViewerQt::ECGViewerQt(const QVector<double>& t,
                         const QVector<double>& vOrig,
                         const QVector<double>& vClean,
                         const QVector<unsigned char>& artMask,
                         double fs,
                         double window_s,
                         bool has_ylim,
                         double ymin,
                         double ymax,
                         bool as_mv,
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

    // Axes labels
    plot_->xAxis->setLabel("Time (s)");
    plot_->yAxis->setLabel(as_mv ? "Voltage (mV)" : "Voltage (V)");
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

    // Slider + buttons 
    auto* hbox = new QHBoxLayout();

    btnLeft_ = new QPushButton("Left", central);
    btnRight_ = new QPushButton("Right", central);
    btnZoomIn_ = new QPushButton("Zoom In", central);
    btnZoomOut_ = new QPushButton("Zoom Out", central);
    btnResetView_ = new QPushButton("Reset View", central);
    btnExit_ = new QPushButton("Exit", central);
    btnZoomRect_ = new QPushButton("Rect Zoom", central);
    btnZoomRect_->setCheckable(true);

    slider_ = new QSlider(Qt::Horizontal, central);

    slider_->setMinimum(0);
    slider_->setMaximum(max_start_sample_);
    slider_->setSingleStep(1);

    // Layout: buttons then slider (adjust order as you like)
    hbox->addWidget(btnLeft_);
    hbox->addWidget(btnRight_);
    hbox->addWidget(btnZoomIn_);
    hbox->addWidget(btnZoomOut_);
    hbox->addWidget(btnResetView_);
    hbox->addWidget(btnExit_);
    hbox->addWidget(btnZoomRect_);  // add here
    hbox->addWidget(slider_);

    vbox->addLayout(hbox);

    setCentralWidget(central);
    setWindowTitle("ECG Viewer (Qt)");

    // Connections 
    connect(slider_, &QSlider::valueChanged,
            this, [this](int value) { updateWindow(value); });

    auto buttonStep = [this]() {
        return static_cast<int>(0.2 * window_samples_);
    };

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

            // If you ever want original-artifact-only:
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
    // Remove old items
    for (auto* item : fiducialItems_) {
        plot_->removeItem(item);
    }
    fiducialItems_.clear();

    auto addLinesFor = [this, x0, x1](const QVector<double>& times,
                                      const QString& label,
                                      const QColor& color) {
        for (double t : times) {
            if (t < x0 || t > x1)
                continue;

            // vertical line at t
            auto* line = new QCPItemLine(plot_);
            line->start->setCoords(t, plot_->yAxis->range().lower);
            line->end->setCoords(t, plot_->yAxis->range().upper);
            line->setPen(QPen(color, 0.8, Qt::DashLine));

            auto* txt = new QCPItemText(plot_);
            txt->position->setCoords(t, plot_->yAxis->range().upper);
            txt->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
            txt->setText(QString("%1 @ %2s").arg(label).arg(t, 0, 'f', 5));
            txt->setColor(color);
            txt->setClipToAxisRect(true);
            txt->setRotation(-90);

            fiducialItems_.push_back(line);
            fiducialItems_.push_back(txt);
        }
    };
    addLinesFor(pTimes_, "P", Qt::blue);
    addLinesFor(qTimes_, "Q", Qt::green);
    addLinesFor(rTimes_, "R", Qt::red);
    addLinesFor(sTimes_, "S", Qt::magenta);
    addLinesFor(tTimes_, "T", QColor(255, 140, 0));
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
    default:
        QMainWindow::keyPressEvent(event);
        break;
    }
}

