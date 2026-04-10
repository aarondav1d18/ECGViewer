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

static QString viewerStylesheet()
{
    return R"QSS(
        QMainWindow {
            background-color: #f4f5f7;
        }
        QLabel {
            font-size: 11px;
        }
        QLineEdit, QDoubleSpinBox {
            background: #ffffff;
        }
        QGroupBox {
            font-weight: bold;
            border: none;
            margin-top: 4px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 0px;
            padding: 0px;
        }
        QPushButton {
            padding: 6px 14px;
            border-radius: 6px;
            border: 1px solid #b0b0b0;
            background-color: #ffffff;
        }
        QPushButton:disabled {
            background-color: #d0d0d0;
            border: 1px solid #b0b0b0;
            color: #ffffff;
        }
        QPushButton:checked {
            background-color: #f0f0f0;
        }
        QPushButton:hover {
            background-color: #f0f0f0;
        }
        QPushButton:pressed {
            background-color: #e0e0e0;
        }

        /* Optional: if you set objectName "primaryButton" on a button */
        QPushButton#primaryButton, QPushButton:default {
            background-color: #2f80ed;
            color: #ffffff;
            border: 1px solid #2f80ed;
        }
        QPushButton#primaryButton:hover, QPushButton:default:hover {
            background-color: #2d74d3;
        }
        QPushButton#primaryButton:pressed {
            background-color: #255fb2;
        }

        QStatusBar {
            background-color: #ffffff;
        }

        QTabWidget::pane {
            border: 1px solid #d0d0d0;
            background: #ffffff;
            border-radius: 8px;
        }
        QTabBar::tab {
            background: #ffffff;
            border: 1px solid #d0d0d0;
            padding: 6px 12px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background: #f0f0f0;
        }

        QPushButton#modeButton:checked {
            background-color: #d0d0d0;
            border: 1px solid #a0a0a0;
            color: #707070;
        }

        QPushButton#modeButton:checked:hover {
            background-color: #d0d0d0;
        }

        QListWidget {
            background-color: #ffffff;
            border-radius: 8px;
            border: 1px solid #d0d0d0;
        }
        QLineEdit {
            border-radius: 6px;
            border: 1px solid #d0d0d0;
            padding: 6px;
        }
    )QSS";
}

/**
 * @brief Apply viewer plot styling to match the launcher UI.
 *
 * @details
 * Configures QCustomPlot visual properties to achieve a light, card-style appearance
 * consistent with the Python launcher:
 * - White plot background
 * - Subtle gray axis lines and tick marks
 * - Disabled secondary-axis ticks used only as a visual border
 * - Light grid and sub-grid lines for readability without visual clutter
 *
 * Notes:
 * - QCustomPlot does not support full Qt stylesheets for plot elements, so
 *   styling is applied via pens, brushes, and axis configuration.
 * - Secondary axes (xAxis2/yAxis2) are enabled without ticks or labels to
 *   approximate a plot border in a version-compatible way.
 *
 * This function is safe to call once during construction after plot_ is created.
 */
void ECGViewer::setStyle() {
    
    plot_->axisRect()->setBackground(QBrush(QColor(255, 255, 255)));

    plot_->xAxis->setBasePen(QPen(QColor(208, 208, 208)));
    plot_->yAxis->setBasePen(QPen(QColor(208, 208, 208)));
    plot_->xAxis2->setBasePen(QPen(QColor(208, 208, 208)));
    plot_->yAxis2->setBasePen(QPen(QColor(208, 208, 208)));
    plot_->xAxis2->setVisible(true);
    plot_->yAxis2->setVisible(true);
    plot_->xAxis2->setTicks(false);
    plot_->yAxis2->setTicks(false);
    plot_->xAxis2->setTickLabels(false);
    plot_->yAxis2->setTickLabels(false);

    plot_->xAxis->setBasePen(QPen(QColor(160, 160, 160)));
    plot_->yAxis->setBasePen(QPen(QColor(160, 160, 160)));
    plot_->xAxis->setTickPen(QPen(QColor(160, 160, 160)));
    plot_->yAxis->setTickPen(QPen(QColor(160, 160, 160)));
    plot_->xAxis->setSubTickPen(QPen(QColor(180, 180, 180)));
    plot_->yAxis->setSubTickPen(QPen(QColor(180, 180, 180)));
    plot_->xAxis->setTickLabelColor(QColor(60, 60, 60));
    plot_->yAxis->setTickLabelColor(QColor(60, 60, 60));
    plot_->xAxis->setLabelColor(QColor(60, 60, 60));
    plot_->yAxis->setLabelColor(QColor(60, 60, 60));

    plot_->xAxis->grid()->setPen(QPen(QColor(220, 220, 220)));
    plot_->yAxis->grid()->setPen(QPen(QColor(220, 220, 220)));
    plot_->xAxis->grid()->setSubGridPen(QPen(QColor(235, 235, 235)));
    plot_->yAxis->grid()->setSubGridPen(QPen(QColor(235, 235, 235)));
    plot_->xAxis->grid()->setSubGridVisible(true);
    plot_->yAxis->grid()->setSubGridVisible(true);
}

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
                     bool colour_blind_mode,
                     const QVector<double>& pTimes,
                     const QVector<double>& pVals,
                     const QVector<double>& psTimes,
                     const QVector<double>& psVals,
                     const QVector<double>& peTimes,
                     const QVector<double>& peVals,
                     const QVector<double>& qTimes,
                     const QVector<double>& qVals,
                     const QVector<double>& rTimes,
                     const QVector<double>& rVals,
                     const QVector<double>& sTimes,
                     const QVector<double>& sVals,
                     const QVector<double>& tTimes,
                     const QVector<double>& tVals,
                     const QVector<double>& tsTimes,
                     const QVector<double>& tsVals,
                     const QVector<double>& teTimes,
                     const QVector<double>& teVals,
                     const QString& filePrefix,
                     QWidget* parent)
    : QMainWindow(parent),
      t_(t),
      vOrig_(vOrig),
      vClean_(vClean),
      artMask_(artMask),
      pTimes_(pTimes), pVals_(pVals),
      psTimes_(psTimes), psVals_(psVals),
      peTimes_(peTimes), peVals_(peVals),
      qTimes_(qTimes), qVals_(qVals),
      rTimes_(rTimes), rVals_(rVals),
      sTimes_(sTimes), sVals_(sVals),
      tTimes_(tTimes), tVals_(tVals),
      tsTimes_(tsTimes), tsVals_(tsVals),
      teTimes_(teTimes), teVals_(teVals),
      fs_(fs),
      window_s_(window_s),
      hide_artifacts_(hide_artifacts),
      filePrefix_(filePrefix),
      useColourBlindPalette_(colour_blind_mode)
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
    plot_->setBackground(QBrush(QColor(255, 255, 255)));
    setStyle();

    vbox->addWidget(plot_, 1);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectItems);

    connect(plot_, &QCustomPlot::mousePress, this, &ECGViewer::onPlotMousePress);
    connect(plot_, &QCustomPlot::mouseMove, this, &ECGViewer::onPlotMouseMove);
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


    graphP_ = makeScatterGraph(useColourBlindPalette_ ? QColor("#56B4E9") : QColor("#56B4E9"), QCPScatterStyle::ssDisc, 6); // P: sky blue
    graphQ_ = makeScatterGraph(useColourBlindPalette_ ? QColor("#0072B2") : QColor("#0072B2"), QCPScatterStyle::ssDisc, 6); // Q: blue
    graphR_ = makeScatterGraph(useColourBlindPalette_ ? QColor("#E69F00") : QColor("#E69F00"), QCPScatterStyle::ssTriangle, 8); // R: orange
    graphS_ = makeScatterGraph(useColourBlindPalette_ ? QColor("#CC79A7") : QColor("#CC79A7"), QCPScatterStyle::ssDisc, 6); // S: purple
    graphT_ = makeScatterGraph(useColourBlindPalette_ ? QColor("#D55E00") : QColor("#D55E00"), QCPScatterStyle::ssDisc, 6); // T: vermillion



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
    btnLockNotes_ = new QPushButton("Lock Notes", traversalTab);
    btnZoomRect_->setCheckable(true);
    btnLockNotes_->setCheckable(true);

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
    traversalLayout->addWidget(btnLockNotes_);
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

    QWidget* overlayTab = new QWidget(tabWidget_);
    auto* overlayLayout = new QHBoxLayout(overlayTab);

    btnOverlayToggle_ = new QPushButton("Overlay Mode", overlayTab);
    btnOverlayToggle_->setCheckable(true);
    btnZoomRect_->setObjectName("modeButton");
    btnOverlayToggle_->setObjectName("modeButton");
    btnOverlayLock_ = new QPushButton("Lock Overlays", overlayTab);
    btnOverlayLock_->setCheckable(true);

    btnClearOverlays_ = new QPushButton("Clear Overlays", overlayTab);
    btnShowHide_ = new QPushButton("Show / Hide Overlays", overlayTab);

    overlayLayout->addWidget(btnOverlayToggle_);
    overlayLayout->addWidget(btnClearOverlays_);
    overlayLayout->addWidget(btnShowHide_);
    overlayLayout->addWidget(btnOverlayLock_);
    overlayLayout->addStretch(1);

    overlayTab->setLayout(overlayLayout);
    tabWidget_->addTab(overlayTab, "Overlays");

    vbox->addWidget(tabWidget_);

    setCentralWidget(central);
    setWindowTitle("ECG Viewer (Qt)");
    this->setStyleSheet(viewerStylesheet());

    connect(slider_, &QSlider::valueChanged,
            this, [this](int value) { updateWindow(value); });

    connect(manualInsertButton_, &QPushButton::clicked,
            this, &ECGViewer::onInsertManualFiducial);
    
    connect(btnLockNotes_, &QPushButton::clicked,
            this, [this](bool checked)
    {
        areNotesMoveable_ = true ? areNotesMoveable_ == false : false;
        btnLockNotes_->setText(areNotesMoveable_ ? "Lock Notes" : "Unlock Notes");
        btnLockNotes_->setChecked(!areNotesMoveable_);
    });

    connect(btnOverlayLock_, &QPushButton::clicked,
            this, [this](bool checked)
    {
        areOverlaysMoveable_ = true ? areOverlaysMoveable_ == false : false;
        btnOverlayLock_->setText(areOverlaysMoveable_ ? "Lock Overlays" : "Unlock Overlays");
        btnOverlayLock_->setChecked(areOverlaysMoveable_);
    });

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
        btnZoomRect_->setChecked(zoomRectMode_);
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

    connect(btnOverlayToggle_, &QPushButton::toggled,
            this, [this](bool checked) { setOverlayMode(checked); });

    connect(btnClearOverlays_, &QPushButton::clicked,
            this, [this]() { clearOverlays(); });

    connect(btnShowHide_, &QPushButton::clicked,
            this, [this]() { setOverlaysVisible(!overlaysVisible_); });


    refreshNotesList();
    updateWindow(0);
}

} // namespace ECGViewer
