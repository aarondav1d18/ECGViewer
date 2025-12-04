#pragma once

#include <QMainWindow>
#include <QVector>
#include "qcustomplot.h"

class QCustomPlot;
class QSlider;
class QPushButton;
class QCPGraph;
class QCPAbstractItem;

class ECGViewerQt : public QMainWindow
{
public:
    ECGViewerQt(const QVector<double>& t,
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
                QWidget* parent = nullptr);

    enum class FiducialType { P, Q, R, S, T };

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateWindow(int startSample);
    void nudge(int deltaSamples);
    void updateFiducialLines(double x0, double x1);
    void updateWindowLength(double newWindowSeconds);
    void deleteHoveredFiducial(); 

    QVector<double> t_;
    QVector<double> vOrig_;
    QVector<double> vClean_;
    QVector<unsigned char> artMask_;

    QVector<double> pTimes_, pVals_;
    QVector<double> qTimes_, qVals_;
    QVector<double> rTimes_, rVals_;
    QVector<double> sTimes_, sVals_;
    QVector<double> tTimes_, tVals_;

    double fs_;
    double window_s_;
    int window_samples_;
    int max_start_sample_;
    bool hide_artifacts_;
    bool suppressRangeHandler_ = false;
    bool zoomRectMode_ = false;
    bool blockWindowUpdates_ = false;
    double currentX0{0.0}, currentX1{0.0};
    int hoverFiducialIndex_ = -1;   // index into fiducialsCurrent_ for hover, -1 = none


    double total_time_;
    double min_window_s_;

    double window_s_original_;
    double y_min_orig_;
    double y_max_orig_;

    struct FiducialVisual
    {
        FiducialType type;
        int index;                // index into pTimes_/qTimes_/...
        QCPItemLine* line = nullptr;
        QCPItemText* text = nullptr;
    };

    QVector<FiducialVisual> fiducialsCurrent_;  // items currently visible in window

    bool draggingFiducial_ = false;
    int  activeFiducialIndex_ = -1;
    double dragOffsetSeconds_ = 0.0;            // click offset from fiducial x

    // helpers to get the correct vecs from a type
    QVector<double>& timesFor(FiducialType type);
    QVector<double>& valsFor(FiducialType type);

    QCP::Interactions savedInteractions_;
    QCustomPlot* plot_;
    QSlider* slider_;
    QPushButton* btnLeft_;
    QPushButton* btnRight_;
    QPushButton* btnZoomIn_;
    QPushButton* btnZoomOut_;
    QPushButton* btnResetView_;
    QPushButton* btnExit_;
    QPushButton* btnZoomRect_ = nullptr;
    // UI for tabbed controls
    QTabWidget* tabWidget_ = nullptr;
    QComboBox*  manualTypeCombo_ = nullptr;
    QPushButton* manualInsertButton_ = nullptr;


    QCPGraph* graphCleanBase_;
    QCPGraph* graphCleanNoise_;
    QCPGraph* graphOrigFull_;
    QCPGraph* graphOrigArtifact_;

    QCPGraph* graphP_;
    QCPGraph* graphQ_;
    QCPGraph* graphR_;
    QCPGraph* graphS_;
    QCPGraph* graphT_;

    QVector<QCPAbstractItem*> fiducialItems_;

private slots:
    void onPlotMousePress(QMouseEvent* event);
    void onPlotMouseMove(QMouseEvent* event);
    void onPlotMouseRelease(QMouseEvent* event);
    void onInsertManualFiducial();


};
