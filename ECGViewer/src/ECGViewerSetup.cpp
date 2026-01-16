/**
 * @file ECGViewerSetup.cpp
 * @brief ECGViewer construction, UI layout, and signal/slot wiring.
 *
 * This translation unit builds the main window and connects UI controls:
 * - QCustomPlot initialization and graph setup (clean/original + fiducial scatters)
 * - Traversal controls (slider, zoom controls, reset, rect zoom toggle, save, notes)
 * - Manual fiducial insertion controls
 * - Axis range clamping logic that maps view ranges back to slider positions
 *
 * Per-feature behavior is implemented in the corresponding interaction/plot/annotation files.
 */

#include "ECGViewer.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTabWidget>
#include <QComboBox>
#include <QPen>
#include <QBrush>
#include <QListWidget>
#include <QLineEdit>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

namespace ECGViewer {

/**
 * @brief ECGViewer constructor: initializes state, builds UI, and connects interactions.
 * @details This sets up:
 * - Plot with cleaned ECG + optional original trace
 * - Fiducial scatter graphs
 * - Traversal controls (slider, zoom in/out, reset, rect zoom, notes dialog, save)
 * - Manual fiducial insertion tab
 * - Axis range clamp handler that maps x-range to slider position
 */
ECGViewer::ECGViewer(const QVector<double>& t,
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
                     const QString& filePrefix,
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
      hide_artifacts_(hide_artifacts),
      filePrefix_(filePrefix)
{
    if (t_.size() != vOrig_.size() ||
        t_.size() != vClean_.size() ||
        t_.size() != artMask_.size() ||
        t_.isEmpty()) {
        throw std::runtime_error("All input vectors must be non-empty and of equal length");
    }

    total_time_ = t_.last() - t_.first();
    if (total_time_ <= 0.0) {
        total_time_ = 1.0 / std::max(fs_, 1.0);
    }

    if (window_s_ <= 0.0 || window_s_ > total_time_) {
        window_s_ = total_time_;
    }

    window_s_original_ = window_s_;
    min_window_s_ = std::max(0.05, 5.0 / std::max(fs_, 1.0));

    window_samples_ = static_cast<int>(window_s_ * fs_);
    if (window_samples_ <= 0)
        window_samples_ = std::min(static_cast<int>(t_.size()), 1);

    max_start_sample_ = std::max(0, static_cast<int>(t_.size()) - window_samples_ - 1);

    auto* central = new QWidget(this);
    auto* vbox = new QVBoxLayout(central);

    plot_ = new QCustomPlot(central);
    vbox->addWidget(plot_, 1);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectItems);

    connect(plot_, &QCustomPlot::mousePress,  this, &ECGViewer::onPlotMousePress);
    connect(plot_, &QCustomPlot::mouseMove,   this, &ECGViewer::onPlotMouseMove);
    connect(plot_, &QCustomPlot::mouseRelease,this, &ECGViewer::onPlotMouseRelease);

    plot_->xAxis->setLabel("Time (s)");
    plot_->yAxis->setLabel("Voltage (V)");
    plot_->xAxis->grid()->setVisible(true);
    plot_->yAxis->grid()->setVisible(true);

    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot_->axisRect()->setRangeDrag(Qt::Horizontal);
    plot_->axisRect()->setRangeZoom(Qt::Horizontal);
    plot_->axisRect()->setRangeZoomAxes(plot_->xAxis, plot_->yAxis);

    plot_->selectionRect()->setPen(QPen(Qt::red));
    plot_->selectionRect()->setBrush(QBrush(QColor(255, 0, 0, 50)));

    if (has_ylim) {
        plot_->yAxis->setRange(ymin, ymax);
    } else {
        plot_->yAxis->setRange(-0.1, 0.15);
    }

    y_min_orig_ = plot_->yAxis->range().lower;
    y_max_orig_ = plot_->yAxis->range().upper;

    graphCleanBase_ = plot_->addGraph();
    graphCleanBase_->setPen(QPen(Qt::blue, 1.2));

    graphOrigFull_ = plot_->addGraph();
    {
        QPen p(Qt::gray);
        p.setWidthF(0.8);
        p.setStyle(Qt::SolidLine);
        graphOrigFull_->setPen(p);
    }

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
    graphS_ = makeScatterGraph(Qt::magenta, QCPScatterStyle::ssDisc, 6);
    graphT_ = makeScatterGraph(QColor(255, 140, 0), QCPScatterStyle::ssDisc, 6);

    graphP_->setData(pTimes_, pVals_);
    graphQ_->setData(qTimes_, qVals_);
    graphR_->setData(rTimes_, rVals_);
    graphS_->setData(sTimes_, sVals_);
    graphT_->setData(tTimes_, tVals_);

    tabWidget_ = new QTabWidget(central);
    tabWidget_->setTabPosition(QTabWidget::South);

    QWidget* traversalTab = new QWidget(tabWidget_);
    auto* traversalLayout = new QHBoxLayout(traversalTab);

    btnZoomIn_ = new QPushButton("Zoom In", traversalTab);
    btnZoomOut_ = new QPushButton("Zoom Out", traversalTab);
    btnResetView_ = new QPushButton("Reset View", traversalTab);
    btnExit_ = new QPushButton("Exit", traversalTab);
    btnZoomRect_ = new QPushButton("Rect Zoom", traversalTab);
    btnNotesDialog_ = new QPushButton("Notes…", traversalTab);
    btnSave_ = new QPushButton("Save", traversalTab);
    btnZoomRect_->setCheckable(true);

    slider_ = new QSlider(Qt::Horizontal, traversalTab);
    slider_->setMinimum(0);
    slider_->setMaximum(max_start_sample_);
    slider_->setSingleStep(1);

    traversalLayout->addWidget(btnZoomIn_);
    traversalLayout->addWidget(btnZoomOut_);
    traversalLayout->addWidget(btnResetView_);
    traversalLayout->addWidget(btnExit_);
    traversalLayout->addWidget(btnZoomRect_);
    traversalLayout->addWidget(btnNotesDialog_);
    traversalLayout->addWidget(btnSave_);
    traversalLayout->addWidget(slider_);

    traversalTab->setLayout(traversalLayout);
    tabWidget_->addTab(traversalTab, "Traversal");
    vbox->addWidget(tabWidget_);

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

    setCentralWidget(central);
    setWindowTitle("ECG Viewer (Qt)");

    connect(slider_, &QSlider::valueChanged,
            this, [this](int value) { updateWindow(value); });

    connect(manualInsertButton_, &QPushButton::clicked,
            this, &ECGViewer::onInsertManualFiducial);

    connect(btnZoomRect_, &QPushButton::toggled,
            this, [this](bool checked)
    {
        zoomRectMode_ = checked;
        if (zoomRectMode_) {
            plot_->setInteractions(QCP::iRangeZoom);
            plot_->setSelectionRectMode(QCP::srmZoom);
        } else {
            plot_->setSelectionRectMode(QCP::srmNone);
            plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
        }
    });

    connect(plot_->xAxis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &newRange)
    {
        if (suppressRangeHandler_ || draggingFiducial_)
            return;

        double xLower = newRange.lower;
        double xUpper = newRange.upper;
        double width = newRange.size();

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
        slider_->setValue(startSample);
        suppressRangeHandler_ = false;
    });

    connect(btnSave_, &QPushButton::clicked,
            this, &ECGViewer::onSave);

    connect(btnZoomIn_, &QPushButton::clicked,
            this, [this]() {
                updateWindowLength(window_s_ / 1.5);
            });

    connect(btnZoomOut_, &QPushButton::clicked,
            this, [this]() {
                updateWindowLength(window_s_ * 1.5);
            });

    connect(btnResetView_, &QPushButton::clicked,
            this, [this]() {
                updateWindowLength(window_s_original_);
                plot_->yAxis->setRange(y_min_orig_, y_max_orig_);
                plot_->replot();
            });

    connect(btnExit_, &QPushButton::clicked,
            this, [this]() {
                close();
            });

    connect(plot_, &QCustomPlot::mouseDoubleClick,
            this, &ECGViewer::onPlotMouseDoubleClick);

    connect(btnNotesDialog_, &QPushButton::clicked,
            this, &ECGViewer::onShowNotesDialog);

    refreshNotesList();
    updateWindow(0);
}

} // namespace ECGViewer
