/**
 * @file ECGViewer.hpp
 * @brief Public interface and core state for the Qt-based ECGViewer.
 *
 * This header declares the ECGViewer main window class and its supporting
 * data structures. ECGViewer is a stateful, interactive Qt widget for
 * visualizing ECG time series with:
 *
 * - Windowed scrolling/zooming over time
 * - Overlay of original vs cleaned signals
 * - Fiducial markers (P/Q/R/S/T) with drag-to-edit support
 * - Point notes and time-region notes with persistence
 *
 * Design notes:
 * - The viewer owns all signal data (QVector copies) for lifetime safety.
 * - Rendering is windowed and downsampled for responsiveness.
 * - User interactions (mouse/keyboard) directly mutate backing vectors
 *   and then update plot items incrementally.
 * - Responsibilities are split across multiple translation units:
 *     * Setup/UI wiring        -> ECGViewerSetup.cpp
 *     * Plot/window updates    -> ECGViewerPlot.cpp
 *     * Mouse/keyboard logic   -> ECGViewerInteractions.cpp
 *     * Notes & persistence   -> ECGViewerAnnotations.cpp
 *
 * This header intentionally contains minimal inline documentation; detailed
 * behavior is documented alongside implementations in the corresponding
 * source files.
 */

#pragma once

#include <QMainWindow>
#include <QVector>
#include <QComboBox>
#include <QTabWidget>
#include <QPushButton>
#include <QSlider>
#include <QKeyEvent>
#include <QMouseEvent>

#include <stdexcept>

#include "qcustomplot.h"

// Forward declarations of Qt classes to reduce compile times
// (full includes are in the corresponding .cpp files)
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
    QString tag;
    QString detail;
    double time = 0; // start time (s)
    double duration = 0; // seconds. 0 => point note, >0 => region note
    double volts = 0;
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
              QWidget* parent = nullptr);

    enum class FiducialType { P, Ps, Pe, Q, R, S, T, Ts, Te };

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class OverlayDragMode {
        None,
        Selecting,
        Moving
    };
    struct OverlayVisual {
        QCPGraph* graph = nullptr;
        QCPItemRect* rect = nullptr;

        // Base data captured from the selection (in plot coords).
        QVector<double> baseX;
        QVector<double> baseY;

        // Current bounds (plot coords).
        double x0 = 0.0;
        double x1 = 0.0;
        double y0 = 0.0;
        double y1 = 0.0;

        // Current translation.
        double dx = 0.0;
        double dy = 0.0;

        bool visible = true;
    };

    // Overlay mode UI/state 
    QPushButton* btnOverlayToggle_ = nullptr;
    QPushButton* btnClearOverlays_ = nullptr;
    QPushButton* btnShowHide_ = nullptr;

    bool overlayMode_ = false;
    bool overlaysVisible_ = true;
    const bool useColourBlindPalette_ = false;

    OverlayDragMode overlayDragMode_ = OverlayDragMode::None;

    // Rubber-band rect during selection.
    QCPItemRect* overlayRubberBand_ = nullptr;
    double overlayAnchorX_ = 0.0;
    double overlayAnchorY_ = 0.0;

    // Moving an existing overlay.
    int activeOverlayIndex_ = -1;
    double overlayMoveStartX_ = 0.0;
    double overlayMoveStartY_ = 0.0;
    double overlayMoveOrigDx_ = 0.0;
    double overlayMoveOrigDy_ = 0.0;
    int hoverOverlayIndex_ = -1;

    QVector<OverlayVisual> overlays_;

    void updateWindow(int startSample);
    void nudge(int deltaSamples);
    void updateFiducialLines(double x0, double x1);
    void updateWindowLength(double newWindowSeconds);
    void deleteHoveredFiducial();
    void updateNoteItems(double x0, double x1);
    void openNoteEditor(int noteIndex);
    void deleteHoveredNote();
    void deleteHoveredOverlay();
    void refreshNotesList(); // rebuilds the list from notes_
    void applyNotesFilter(); // filters by search text
    int noteIndexFromItem(QListWidgetItem* item) const;
    void onSave();
    double mouseVoltsClamped(QMouseEvent* e) const;

    void setOverlayMode(bool enabled);
    void clearOverlays();
    void setOverlaysVisible(bool visible);

    bool overlayIndexFromItem(QCPAbstractItem* item, int& outIndex) const;

    void ensureOverlayRubberBand();
    void updateOverlayRubberBand(double x0, double x1);

    void finalizeOverlayFromSelection(double x0, double x1);
    void applyOverlayTransform(OverlayVisual& ov);

    double clampTime(double t) const;
    double cleanValueAtTime(double relTime) const;

    QString fiducialLabel(FiducialType type) const;
    QChar fiducialChar(FiducialType type) const;
    FiducialType fiducialTypeFromText(const QString& s) const;

    QString noteListLine(const Note& n) const;
    bool noteMatchesFilter(const Note& n, const QString& filter) const;

    int createNoteAtTime(double relTime);
    void clampNoteToBounds(Note& n) const;
    void setStyle();

    QDir ensureDataDir() const;

    QVector<double> t_;
    QVector<double> vOrig_;
    QVector<double> vClean_;
    QVector<unsigned char> artMask_;

    QVector<double> pTimes_, pVals_;
    QVector<double> psTimes_, psVals_;
    QVector<double> peTimes_, peVals_;
    QVector<double> qTimes_, qVals_;
    QVector<double> rTimes_, rVals_;
    QVector<double> sTimes_, sVals_;
    QVector<double> tTimes_, tVals_;
    QVector<double> tsTimes_, tsVals_;
    QVector<double> teTimes_, teVals_;

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
    bool creatingRegion_ = false;
    int  creatingNoteIndex_ = -1;
    double regionAnchorTime_ = 0.0;
    bool areNotesMoveable_ = true;
    bool areOverlaysMoveable_ = true;

    double total_time_;
    double min_window_s_;

    double window_s_original_;
    double y_min_orig_;
    double y_max_orig_;

    enum class NoteDragMode { None, Move, ResizeLeft, ResizeRight, CreateRegion };

    NoteDragMode noteDragMode_ = NoteDragMode::None;

    // Used for resize/move math
    double regionPressTime_ = 0.0;      // mouse-down time (x coord)
    double originalStart_ = 0.0;
    double originalEnd_ = 0.0;

    struct FiducialVisual
    {
        FiducialType type;
        int index; // index into pTimes_/qTimes_/...
        QCPItemLine* line = nullptr;
        QCPItemText* text = nullptr;
    };
    struct NoteVisual
    {
        int noteIndex = -1;
        QCPItemLine* line = nullptr;   // for point notes
        QCPItemText* text = nullptr;
        QCPItemRect* rect = nullptr;   // for region notes
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

    struct FiducialRefs
    {
        QVector<double>* times = nullptr;
        QVector<double>* vals = nullptr;
        QCPGraph* graph = nullptr;
        QString label;
        QChar ch = '?';
    };

    // helpers to get the correct vecs/graph/label from a type
    inline FiducialRefs fiducialRefsFor(FiducialType type)
    {
        switch (type) {
        case FiducialType::P: return FiducialRefs{ &pTimes_, &pVals_, graphP_, "P", 'P' };
        case FiducialType::Ps: return FiducialRefs{ &psTimes_, &psVals_, nullptr, "Ps", 'P' };
        case FiducialType::Pe: return FiducialRefs{ &peTimes_, &peVals_, nullptr, "Pe", 'P' };
        case FiducialType::Q: return FiducialRefs{ &qTimes_, &qVals_, graphQ_, "Q", 'Q' };
        case FiducialType::R: return FiducialRefs{ &rTimes_, &rVals_, graphR_, "R", 'R' };
        case FiducialType::S: return FiducialRefs{ &sTimes_, &sVals_, graphS_, "S", 'S' };
        case FiducialType::T: return FiducialRefs{ &tTimes_, &tVals_, graphT_, "T", 'T' };
        case FiducialType::Ts: return FiducialRefs{ &tsTimes_, &tsVals_, nullptr, "Ts", 'T' };
        case FiducialType::Te: return FiducialRefs{ &teTimes_, &teVals_, nullptr, "Te", 'T' };
        }
        throw std::runtime_error("Invalid FiducialType");
    }

    inline void refreshFiducialGraph(FiducialType type)
    {
        auto r = fiducialRefsFor(type);
        if (!r.graph || !r.times || !r.vals)
            return;
        r.graph->setData(*r.times, *r.vals);
    }

    inline double minNoteDurationSeconds() const
    {
        return 1.0 / std::max(fs_, 1.0);
    }

    inline double mouseTimeClamped(const QMouseEvent* event) const
    {
        return clampTime(plot_->xAxis->pixelToCoord(event->pos().x()));
    }

    inline void beginItemDrag(Qt::CursorShape cursor)
    {
        savedInteractions_ = plot_->interactions();
        plot_->setInteraction(QCP::iRangeDrag, false);
        setCursor(cursor);
    }

    inline void endItemDrag()
    {
        setCursor(Qt::ArrowCursor);
        plot_->setInteractions(savedInteractions_);
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
    QPushButton* btnLockNotes_;
    QPushButton* btnOverlayLock_;
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
