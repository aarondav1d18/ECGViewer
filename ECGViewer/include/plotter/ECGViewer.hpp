#pragma once

#include <QMainWindow>
#include <QVector>
#include <QComboBox>
#include <QTabWidget>
#include <QPushButton>
#include <QSlider>
#include <QKeyEvent>
#include <QMouseEvent>

#include "qcustomplot.h"

class QCustomPlot;
class QSlider;
class QPushButton;
class QCPGraph;
class QCPAbstractItem;
class QListWidget;
class QLineEdit;
class QListWidgetItem;

namespace ECGViewer {

struct Note
{
    QString tag; // easier with Qt widgets
    QString detail; // free-text detail
    double time = 0; // seconds (relative, like fiducials)
    double volts = 0; // optional; can be y-value at note time
};


class ECGViewer : public QMainWindow
{
public:
    ECGViewer(const QVector<double>& t,
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
    void updateNoteItems(double x0, double x1);
    void openNoteEditor(int noteIndex);
    void deleteHoveredNote();
    void refreshNotesList(); // rebuilds the list from notes_
    void applyNotesFilter(); // filters by search text
    int noteIndexFromItem(QListWidgetItem* item) const;
    void onSave();

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
    int hoverFiducialIndex_ = -1; // index into fiducialsCurrent_ for hover, -1 = none
    QString filePrefix_;


    double total_time_;
    double min_window_s_;

    double window_s_original_;
    double y_min_orig_;
    double y_max_orig_;

    struct FiducialVisual
    {
        FiducialType type;
        int index; // index into pTimes_/qTimes_/...
        QCPItemLine* line = nullptr;
        QCPItemText* text = nullptr;
    };
    struct NoteVisual
    {
        int noteIndex = -1; // index into notes_
        QCPItemLine* line = nullptr;
        QCPItemText* text = nullptr;
    };

    QVector<FiducialVisual> fiducialsCurrent_;  // items currently visible in window

    bool draggingFiducial_ = false;
    int  activeFiducialIndex_ = -1;
    double dragOffsetSeconds_ = 0.0; // click offset from fiducial x

    QVector<Note> notes_; // all notes (time, tag, detail, volts)
    QVector<NoteVisual> notesCurrent_; // only notes visible in current window

    int hoverNoteIndex_ = -1; // index into notesCurrent_, -1 = none
    bool draggingNote_ = false;
    int  activeNoteVisualIndex_ = -1;
    double noteDragOffsetSeconds_ = 0.0;


    // helpers to get the correct vecs from a type
    inline QVector<double>& timesFor(FiducialType type)
    {
        switch (type) {
        case FiducialType::P: return pTimes_;
        case FiducialType::Q: return qTimes_;
        case FiducialType::R: return rTimes_;
        case FiducialType::S: return sTimes_;
        case FiducialType::T: return tTimes_;
        }

        throw std::runtime_error("Invalid FiducialType in timesFor()");
    }


    inline QVector<double>& valsFor(FiducialType type)
    {
        switch (type) {
        case FiducialType::P: return pVals_;
        case FiducialType::Q: return qVals_;
        case FiducialType::R: return rVals_;
        case FiducialType::S: return sVals_;
        case FiducialType::T: return tVals_;
        }
        
        throw std::runtime_error("Invalid FiducialType in valsFor()");
    }

    QCP::Interactions savedInteractions_;
    QCustomPlot* plot_;
    QSlider* slider_;
    QPushButton* btnZoomIn_;
    QPushButton* btnZoomOut_;
    QPushButton* btnResetView_;
    QPushButton* btnExit_;
    QPushButton* btnZoomRect_ = nullptr;
    QPushButton* btnNewNote_;
    QPushButton* btnSaveNotes_;
    QPushButton* btnLoadNotes_;
    QPushButton* btnDeleteNote_;
    QPushButton* btnSave_;
    // UI for tabbed controls
    QTabWidget* tabWidget_ = nullptr;
    QComboBox*  manualTypeCombo_ = nullptr;
    QPushButton* manualInsertButton_ = nullptr;
    QListWidget* notesListWidget_ = nullptr;
    QLineEdit* notesSearchEdit_ = nullptr;
    QPushButton* btnNotesDialog_ = nullptr;



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
    void onPlotMouseDoubleClick(QMouseEvent* event);
    void updatePoint(FiducialVisual& f, double newTime);
    void onNewNote();
    void onSaveNotes(const bool guiSave = false);
    void onLoadNotes();
    void onDeleteNoteFromList();
    void onNotesListItemDoubleClicked(QListWidgetItem* item);
    void onNotesSearchTextChanged(const QString& text);
    void onShowNotesDialog();

};
} // namespace ECGViewer
