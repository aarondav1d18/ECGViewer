#pragma once

#include <QMainWindow>
#include <QVector>

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
                QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateWindow(int startSample);
    void nudge(int deltaSamples);
    void updateFiducialLines(double x0, double x1);
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

    double fs_;
    double window_s_;
    int window_samples_;
    int max_start_sample_;
    bool hide_artifacts_;

    bool zoomRectMode_ = false;


    double total_time_;
    double min_window_s_;

    double window_s_original_;
    double y_min_orig_;
    double y_max_orig_;

    QCustomPlot* plot_;
    QSlider* slider_;
    QPushButton* btnLeft_;
    QPushButton* btnRight_;
    QPushButton* btnZoomIn_;
    QPushButton* btnZoomOut_;
    QPushButton* btnResetView_;
    QPushButton* btnExit_;
    QPushButton* btnZoomRect_ = nullptr;

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

};
