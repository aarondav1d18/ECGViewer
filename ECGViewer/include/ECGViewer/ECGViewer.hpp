#pragma once

#include <QMainWindow>
#include <QVector>
#include <QPushButton>
#include <QSlider>
#include <QKeyEvent>

#include <stdexcept>

#include "qcustomplot.h"

class QCustomPlot;
class QSlider;
class QPushButton;
class QCPGraph;

namespace ECGViewer {

class ECGViewer : public QMainWindow
{
public:
    ECGViewer(QVector<double> t,
            QVector<double> vOrig,
            QVector<double> vClean,
            QVector<unsigned char> artMask,
            double fs,
            double window_s,
            bool has_ylim,
            double ymin,
            double ymax,
            bool hide_artifacts,
            QVector<double> pTimes,
            QVector<double> pVals,
            QVector<double> qTimes,
            QVector<double> qVals,
            QVector<double> rTimes,
            QVector<double> rVals,
            QVector<double> sTimes,
            QVector<double> sVals,
            QVector<double> tTimes,
            QVector<double> tVals,
            QString filePrefix,
            QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateWindow(int startSample);
    void nudge(int deltaSamples);
    void updateWindowLength(double newWindowSeconds);

    QVector<double> t_;
    QVector<double> vOrig_;
    QVector<double> vClean_;
    QVector<unsigned char> artMask_;

    QVector<double> pTimes_, pVals_;
    QVector<double> qTimes_, qVals_;
    QVector<double> rTimes_, rVals_;
    QVector<double> sTimes_, sVals_;
    QVector<double> tTimes_, tVals_;

    QCPGraph* graphP_ = nullptr;
    QCPGraph* graphQ_ = nullptr;
    QCPGraph* graphR_ = nullptr;
    QCPGraph* graphS_ = nullptr;
    QCPGraph* graphT_ = nullptr;
    QString filePrefix_;

    QVector<QCPAbstractItem*> fiducialItems_;

    void updateFiducialLines(double x0, double x1);

    double fs_;
    double window_s_;
    int window_samples_;
    int max_start_sample_;
    bool hide_artifacts_;

    bool suppressRangeHandler_ = false;
    bool zoomRectMode_ = false;

    double total_time_;
    double min_window_s_;

    double window_s_original_;
    double y_min_orig_;
    double y_max_orig_;

    QCustomPlot* plot_ = nullptr;
    QSlider* slider_ = nullptr;

    QPushButton* btnZoomIn_ = nullptr;
    QPushButton* btnZoomOut_ = nullptr;
    QPushButton* btnResetView_ = nullptr;
    QPushButton* btnExit_ = nullptr;
    QPushButton* btnZoomRect_ = nullptr;

    QCPGraph* graphCleanBase_ = nullptr;
    QCPGraph* graphOrigFull_ = nullptr;
};

} // namespace ECGViewer
