#include "ECGViewer.hpp"

#include <QString>
#include <QInputDialog>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace ECGViewer {

double ECGViewer::clampTime(double t) const
{
    if (t < 0.0) return 0.0;
    if (t > total_time_) return total_time_;
    return t;
}

/**
 * @brief Sample the cleaned signal at a relative time.
 * @details Uses nearest-sample rounding based on fs_, and clamps index to [0, vClean_.size()-1].
 */
double ECGViewer::cleanValueAtTime(double relTime) const
{
    relTime = clampTime(relTime);

    double absTime = t_.first() + relTime;
    int idx = static_cast<int>(std::round((absTime - t_.first()) * fs_));
    if (idx < 0) idx = 0;
    if (idx >= vClean_.size()) idx = vClean_.size() - 1;
    return vClean_[idx];
}

void ECGViewer::refreshFiducialGraph(FiducialType type)
{
    switch (type) {
    case FiducialType::P: graphP_->setData(pTimes_, pVals_); break;
    case FiducialType::Q: graphQ_->setData(qTimes_, qVals_); break;
    case FiducialType::R: graphR_->setData(rTimes_, rVals_); break;
    case FiducialType::S: graphS_->setData(sTimes_, sVals_); break;
    case FiducialType::T: graphT_->setData(tTimes_, tVals_); break;
    }
}

QString ECGViewer::fiducialLabel(FiducialType type) const
{
    switch (type) {
    case FiducialType::P: return "P";
    case FiducialType::Q: return "Q";
    case FiducialType::R: return "R";
    case FiducialType::S: return "S";
    case FiducialType::T: return "T";
    }
    return "?";
}

QChar ECGViewer::fiducialChar(FiducialType type) const
{
    switch (type) {
    case FiducialType::P: return 'P';
    case FiducialType::Q: return 'Q';
    case FiducialType::R: return 'R';
    case FiducialType::S: return 'S';
    case FiducialType::T: return 'T';
    }
    return '?';
}

ECGViewer::FiducialType ECGViewer::fiducialTypeFromText(const QString& s) const
{
    if (s == "P") return FiducialType::P;
    if (s == "Q") return FiducialType::Q;
    if (s == "S") return FiducialType::S;
    if (s == "T") return FiducialType::T;
    return FiducialType::R;
}

/**
 * @brief Build a one-line list summary for a note.
 * @details Includes time + tag, and a short snippet of detail when present.
 */
QString ECGViewer::noteListLine(const Note& n) const
{
    QString line = QString("%1s  |  %2")
                       .arg(n.time, 0, 'f', 3)
                       .arg(n.tag.isEmpty() ? QStringLiteral("Note") : n.tag);

    if (!n.detail.isEmpty()) {
        QString snippet = n.detail;
        snippet.replace('\n', ' ');
        if (snippet.size() > 60)
            snippet = snippet.left(57) + "...";
        line += "  |  " + snippet;
    }

    return line;
}

bool ECGViewer::noteMatchesFilter(const Note& n, const QString& filter) const
{
    if (filter.isEmpty())
        return true;
    QString haystack = n.tag + " " + n.detail;
    return haystack.contains(filter, Qt::CaseInsensitive);
}

int ECGViewer::createNoteAtTime(double relTime)
{
    Note n;
    n.time = clampTime(relTime);
    n.volts = cleanValueAtTime(n.time);
    n.tag = QStringLiteral("Note %1").arg(notes_.size() + 1);
    n.detail = QString();
    n.duration = 0.0;
    notes_.push_back(n);
    return notes_.size() - 1;
}

/**
 * @brief Clamp note fields to safe bounds.
 * @details Ensures time in range and region end does not exceed total_time_.
 */
void ECGViewer::clampNoteToBounds(Note& n) const
{
    n.time = clampTime(n.time);

    if (n.duration < 0.0)
        n.duration = 0.0;

    if (n.time + n.duration > total_time_)
        n.duration = std::max(0.0, total_time_ - n.time);
}

QDir ECGViewer::ensureDataDir() const
{
    QDir dir("./ECGData");
    if (!dir.exists())
        dir.mkpath(".");
    return dir;
}

/**
 * @brief Insert a fiducial point at the center of the current window.
 * @details Computes the new X as the window midpoint, samples Y from vClean_,
 * inserts into sorted backing vectors, refreshes scatter and re-builds line items.
 */
void ECGViewer::onInsertManualFiducial()
{
    QString choice = manualTypeCombo_ ? manualTypeCombo_->currentText() : QString("R");
    FiducialType type = fiducialTypeFromText(choice);

    double newTime = clampTime(0.5 * (currentX0 + currentX1));
    double newVal = cleanValueAtTime(newTime);

    QVector<double>& times = timesFor(type);
    QVector<double>& vals = valsFor(type);

    int insertIndex = 0;
    while (insertIndex < times.size() && times[insertIndex] < newTime)
        ++insertIndex;

    times.insert(insertIndex, newTime);
    vals.insert(insertIndex, newVal);

    refreshFiducialGraph(type);

    updateFiducialLines(currentX0, currentX1);
    plot_->replot();
}

void ECGViewer::onNewNote()
{
    double newTime = clampTime(0.5 * (currentX0 + currentX1));
    int idx = createNoteAtTime(newTime);

    updateNoteItems(currentX0, currentX1);
    plot_->replot();

    openNoteEditor(idx);
}

/**
 * @brief Edit a note in a modal dialog.
 * @details Updates note fields only if accepted, then clamps and refreshes visuals.
 */
void ECGViewer::openNoteEditor(int noteIndex)
{
    if (noteIndex < 0 || noteIndex >= notes_.size())
        return;

    Note& n = notes_[noteIndex];

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Edit Note"));

    QFormLayout* form = new QFormLayout(&dlg);

    auto* tagEdit = new QLineEdit(&dlg);
    tagEdit->setText(n.tag);
    form->addRow(QStringLiteral("Tag:"), tagEdit);

    auto* timeSpin = new QDoubleSpinBox(&dlg);
    timeSpin->setDecimals(5);
    timeSpin->setRange(0.0, total_time_);
    timeSpin->setValue(n.time);
    form->addRow(QStringLiteral("Time (s):"), timeSpin);

    auto* durSpin = new QDoubleSpinBox(&dlg);
    durSpin->setDecimals(5);
    durSpin->setRange(0.0, total_time_);
    durSpin->setValue(n.duration);
    form->addRow(QStringLiteral("Duration (s):"), durSpin);

    auto* voltsSpin = new QDoubleSpinBox(&dlg);
    voltsSpin->setDecimals(5);
    voltsSpin->setRange(-1000.0, 1000.0);
    voltsSpin->setValue(n.volts);
    form->addRow(QStringLiteral("Voltage (V):"), voltsSpin);

    auto* detailEdit = new QTextEdit(&dlg);
    detailEdit->setPlainText(n.detail);
    form->addRow(QStringLiteral("Detail:"), detailEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dlg);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        n.tag = tagEdit->text();
        n.time = timeSpin->value();
        n.duration = durSpin->value();
        n.volts = voltsSpin->value();
        n.detail = detailEdit->toPlainText();

        clampNoteToBounds(n);

        updateNoteItems(currentX0, currentX1);
        plot_->replot();
    }
}

void ECGViewer::deleteHoveredNote()
{
    if (hoverNoteIndex_ < 0 || hoverNoteIndex_ >= notesCurrent_.size())
        return;

    const auto nv = notesCurrent_[hoverNoteIndex_];
    int noteIndex = nv.noteIndex;
    if (noteIndex < 0 || noteIndex >= notes_.size())
        return;

    notes_.remove(noteIndex);

    updateNoteItems(currentX0, currentX1);
    plot_->replot();
}

void ECGViewer::refreshNotesList()
{
    if (!notesListWidget_)
        return;

    notesListWidget_->clear();

    const QString filter = notesSearchEdit_ ? notesSearchEdit_->text().trimmed() : QString();

    for (int i = 0; i < notes_.size(); ++i) {
        const Note& n = notes_[i];

        if (!noteMatchesFilter(n, filter))
            continue;

        auto* item = new QListWidgetItem(noteListLine(n), notesListWidget_);
        item->setData(Qt::UserRole, i);
    }
}

void ECGViewer::applyNotesFilter()
{
    refreshNotesList();
}

void ECGViewer::onNotesSearchTextChanged(const QString& /*text*/)
{
    applyNotesFilter();
}

int ECGViewer::noteIndexFromItem(QListWidgetItem* item) const
{
    if (!item)
        return -1;
    bool ok = false;
    int idx = item->data(Qt::UserRole).toInt(&ok);
    if (!ok)
        return -1;
    if (idx < 0 || idx >= notes_.size())
        return -1;
    return idx;
}

/**
 * @brief Jump the viewing window to center around the selected note.
 * @details Updates x-axis range and then refreshes the window by sample index.
 */
void ECGViewer::onNotesListItemDoubleClicked(QListWidgetItem* item)
{
    int noteIndex = noteIndexFromItem(item);
    if (noteIndex < 0)
        return;

    const Note& n = notes_[noteIndex];

    double half = window_s_ * 0.5;
    double x0 = std::max(0.0, n.time - half);
    double x1 = std::min(total_time_, x0 + window_s_);

    currentX0 = x0;
    currentX1 = x1;

    plot_->xAxis->setRange(x0, x1);

    int startSample = static_cast<int>(x0 * fs_);
    if (startSample < 0) startSample = 0;
    if (startSample > max_start_sample_) startSample = max_start_sample_;

    updateWindow(startSample);

    refreshNotesList();
}

void ECGViewer::onDeleteNoteFromList()
{
    if (!notesListWidget_)
        return;

    QListWidgetItem* item = notesListWidget_->currentItem();
    if (!item)
        return;

    int noteIndex = noteIndexFromItem(item);
    if (noteIndex < 0)
        return;

    notes_.remove(noteIndex);

    updateNoteItems(currentX0, currentX1);
    refreshNotesList();
    plot_->replot();
}

/**
 * @brief Save notes as JSON.
 * @details If guiSave is false, writes to ./ECGData/<prefix>_ecg_data.json without prompting.
 * If guiSave is true, shows a file dialog.
 */
void ECGViewer::onSaveNotes(const bool guiSave)
{
    if (notes_.isEmpty() && guiSave) {
        QMessageBox::information(this, "Save Notes", "There are no notes to save.");
        return;
    }

    if (!guiSave) {
        QDir dir = ensureDataDir();
        std::cout << "Data folder path: " << dir.path().toStdString() << std::endl;

        QString fileNameNotes = dir.filePath(QString("%1_ecg_data.json").arg(filePrefix_));
        std::cout << "Saving to file: " << fileNameNotes.toStdString() << std::endl;

        QJsonArray arr;
        for (const auto& n : notes_) {
            QJsonObject o;
            o["tag"] = n.tag;
            o["detail"] = n.detail;
            o["time"] = n.time;
            o["duration"] = n.duration;
            o["volts"] = n.volts;
            arr.append(o);
        }

        QJsonDocument doc(arr);

        QFile f(fileNameNotes);
        if (!f.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "Save Notes",
                                 "Could not open file for writing:\n" + fileNameNotes);
            return;
        }

        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Save Notes",
        QString(),
        "Notes JSON (*.json);;All Files (*)"
    );

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.suffix().isEmpty())
        fileName += ".json";

    QJsonArray arr;
    for (const auto& n : notes_) {
        QJsonObject o;
        o["tag"] = n.tag;
        o["detail"] = n.detail;
        o["time"] = n.time;
        o["volts"] = n.volts;
        o["duration"] = n.duration;
        arr.append(o);
    }

    QJsonDocument doc(arr);

    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Save Notes",
                             "Could not open file for writing:\n" + fileName);
        return;
    }

    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
}

void ECGViewer::onSave()
{
    if (!notes_.isEmpty())
        onSaveNotes(false);

    QDir dir = ensureDataDir();
    std::cout << "Data folder path: " << dir.path().toStdString() << std::endl;

    QString fileName = dir.filePath(QString("%1_ecg_data.csv").arg(filePrefix_));
    std::cout << "Saving to file: " << fileName.toStdString() << std::endl;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save ECG Data",
                             "Could not open file for writing:\n" + fileName);
        return;
    }

    QTextStream out(&file);
    out << "Tag,Time,Voltage\n";

    std::vector<FiducialType> types = {
        FiducialType::P,
        FiducialType::Q,
        FiducialType::R,
        FiducialType::S,
        FiducialType::T
    };

    for (const auto& type : types) {
        QVector<double>& vals = valsFor(type);
        QVector<double>& times = timesFor(type);

        QChar tagChar = fiducialChar(type);
        for (int i = 0; i < times.size(); ++i) {
            out << tagChar << "," << times[i] << "," << vals[i] << "\n";
        }
    }

    file.close();
}

void ECGViewer::onLoadNotes()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Load Notes",
        QString(),
        "Notes JSON (*.json);;All Files (*)");

    if (fileName.isEmpty())
        return;

    QFile f(fileName);

    if (!QFileInfo(f).fileName().startsWith(filePrefix_)) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Load Notes",
                                      "The selected notes file does not match the current ECG data prefix.\n"
                                      "Are you sure you want to load it?",
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
    }

    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Load Notes",
                             "Could not open file for reading:\n" + fileName);
        return;
    }

    QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, "Load Notes",
                             "JSON parse error:\n" + err.errorString());
        return;
    }

    if (!doc.isArray()) {
        QMessageBox::warning(this, "Load Notes",
                             "Invalid notes file (expected JSON array).");
        return;
    }

    QJsonArray arr = doc.array();

    QVector<Note> loaded;
    loaded.reserve(arr.size());

    for (const auto& v : arr) {
        if (!v.isObject())
            continue;

        QJsonObject o = v.toObject();

        Note n;
        n.tag = o.value("tag").toString();
        n.detail = o.value("detail").toString();
        n.time = o.value("time").toDouble();
        n.volts = o.value("volts").toDouble();
        n.duration = o.value("duration").toDouble(0.0);

        clampNoteToBounds(n);

        loaded.push_back(n);
    }

    notes_ = loaded;

    updateNoteItems(currentX0, currentX1);
    refreshNotesList();
    plot_->replot();
}

/**
 * @brief Modal notes manager dialog with search/list/edit/delete/save/load.
 * @details Uses local widgets; the backing data remains notes_.
 */
void ECGViewer::onShowNotesDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Notes");
    dlg.resize(700, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(&dlg);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    QLabel* searchLabel = new QLabel("Search:", &dlg);
    QLineEdit* searchEdit = new QLineEdit(&dlg);
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(searchEdit);
    mainLayout->addLayout(searchLayout);

    QListWidget* list = new QListWidget(&dlg);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(list, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnNew = new QPushButton("New", &dlg);
    QPushButton* btnEdit = new QPushButton("Edit", &dlg);
    QPushButton* btnDelete = new QPushButton("Delete", &dlg);
    QPushButton* btnSave = new QPushButton("Save", &dlg);
    QPushButton* btnLoad = new QPushButton("Load", &dlg);
    QPushButton* btnClose = new QPushButton("Close", &dlg);

    btnLayout->addWidget(btnNew);
    btnLayout->addWidget(btnEdit);
    btnLayout->addWidget(btnDelete);
    btnLayout->addStretch(1);
    btnLayout->addWidget(btnSave);
    btnLayout->addWidget(btnLoad);
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);

    auto refreshList = [this, list, searchEdit]()
    {
        list->clear();
        QString filter = searchEdit->text().trimmed();

        for (int i = 0; i < notes_.size(); ++i) {
            const Note& n = notes_[i];

            if (!noteMatchesFilter(n, filter))
                continue;

            QListWidgetItem* item = new QListWidgetItem(noteListLine(n), list);
            item->setData(Qt::UserRole, i);
        }
    };

    auto currentNoteIndex = [list]() -> int
    {
        QListWidgetItem* item = list->currentItem();
        if (!item)
            return -1;
        bool ok = false;
        int idx = item->data(Qt::UserRole).toInt(&ok);
        if (!ok)
            return -1;
        return idx;
    };

    QObject::connect(searchEdit, &QLineEdit::textChanged,
                     &dlg, [refreshList]() { refreshList(); });

    QObject::connect(btnNew, &QPushButton::clicked,
                     &dlg, [this, refreshList]()
    {
        double newTime = clampTime(0.5 * (currentX0 + currentX1));
        int idx = createNoteAtTime(newTime);

        openNoteEditor(idx);
        updateNoteItems(currentX0, currentX1);
        plot_->replot();
        refreshList();
    });

    QObject::connect(btnEdit, &QPushButton::clicked,
                     &dlg, [this, currentNoteIndex, refreshList]()
    {
        int idx = currentNoteIndex();
        if (idx < 0 || idx >= notes_.size())
            return;

        openNoteEditor(idx);
        updateNoteItems(currentX0, currentX1);
        plot_->replot();
        refreshList();
    });

    QObject::connect(btnDelete, &QPushButton::clicked,
                     &dlg, [this, currentNoteIndex, refreshList]()
    {
        int idx = currentNoteIndex();
        if (idx < 0 || idx >= notes_.size())
            return;

        notes_.remove(idx);
        updateNoteItems(currentX0, currentX1);
        plot_->replot();
        refreshList();
    });

    QObject::connect(btnSave, &QPushButton::clicked,
                     &dlg, [this]()
    {
        onSaveNotes(true);
    });

    QObject::connect(btnLoad, &QPushButton::clicked,
                     &dlg, [this, refreshList]()
    {
        onLoadNotes();
        refreshList();
    });

    QObject::connect(btnClose, &QPushButton::clicked,
                     &dlg, &QDialog::accept);

    QObject::connect(list, &QListWidget::itemDoubleClicked,
                     &dlg, [this, list](QListWidgetItem* item)
    {
        if (!item) return;
        int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= notes_.size())
            return;

        const Note& n = notes_[idx];

        double half = window_s_ * 0.5;
        double x0 = std::max(0.0, n.time - half);
        double x1 = std::min(total_time_, x0 + window_s_);
        currentX0 = x0;
        currentX1 = x1;
        plot_->xAxis->setRange(x0, x1);

        int startSample = static_cast<int>(x0 * fs_);
        if (startSample < 0) startSample = 0;
        if (startSample > max_start_sample_) startSample = max_start_sample_;
        updateWindow(startSample);

        openNoteEditor(idx);
    });

    refreshList();
    dlg.exec();
}

} // namespace ECGViewer
