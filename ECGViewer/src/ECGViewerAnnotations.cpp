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
#include <iostream>
namespace ECGViewer {
void ECGViewer::onInsertManualFiducial()
{
    QString choice = manualTypeCombo_ ? manualTypeCombo_->currentText() : QString("R");

    FiducialType type = FiducialType::R;
    if (choice == "P") type = FiducialType::P;
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
    plot_->replot();
}

void ECGViewer::onNewNote()
{
    // place at centre of current window
    double newTime = 0.5 * (currentX0 + currentX1);

    // clamp
    if (newTime < 0.0) newTime = 0.0;
    if (newTime > total_time_) newTime = total_time_;

    // sample volts from clean signal
    double absTime = t_.first() + newTime;
    int sampleIndex = static_cast<int>(std::round((absTime - t_.first()) * fs_));
    if (sampleIndex < 0) sampleIndex = 0;
    if (sampleIndex >= vClean_.size()) sampleIndex = vClean_.size() - 1;
    double val = vClean_[sampleIndex];

    Note n;
    n.time = newTime;
    n.volts = val;
    n.tag = QStringLiteral("Note %1").arg(notes_.size() + 1);
    n.detail = QString();

    notes_.push_back(n);

    // Refresh visuals
    updateNoteItems(currentX0, currentX1);
    plot_->replot();

    // open editor immediately:
    openNoteEditor(notes_.size() - 1);
}

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
    // a bit generous
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

        // Clamp
        if (n.time < 0.0) n.time = 0.0;
        if (n.time > total_time_) n.time = total_time_;

        // Ensure region doesn't run past end
        if (n.duration < 0.0) n.duration = 0.0;
        if (n.time + n.duration > total_time_)
            n.duration = std::max(0.0, total_time_ - n.time);


        // Recreate visuals
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

    // remove actual note
    notes_.remove(noteIndex);

    // after removal, just rebuild note items for current window
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

        // Apply filter (simple substring on tag or detail)
        if (!filter.isEmpty()) {
            QString haystack = n.tag + " " + n.detail;
            if (!haystack.contains(filter, Qt::CaseInsensitive))
                continue;
        }

        auto* item = new QListWidgetItem(line, notesListWidget_);
        item->setData(Qt::UserRole, i);  // store note index
    }
}

void ECGViewer::applyNotesFilter()
{
    refreshNotesList(); // we’re rebuilding list with filter applied
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

void ECGViewer::onNotesListItemDoubleClicked(QListWidgetItem* item)
{
    int noteIndex = noteIndexFromItem(item);
    if (noteIndex < 0)
        return;

    const Note& n = notes_[noteIndex];

    // Center the window around this note
    double half = window_s_ * 0.5;
    double x0 = std::max(0.0, n.time - half);
    double x1 = std::min(total_time_, x0 + window_s_);

    currentX0 = x0;
    currentX1 = x1;

    plot_->xAxis->setRange(x0, x1);

    // Update window by sample index
    int startSample = static_cast<int>(x0 * fs_);
    if (startSample < 0) startSample = 0;
    if (startSample > max_start_sample_) startSample = max_start_sample_;

    updateWindow(startSample);

    // Open the note editor popup
    // Dont know if this would be good to do automatically when jumping to notes.
    // going to leave it for now and maybe re enable it later.
    // openNoteEditor(noteIndex);

    // Refresh list in case tag/time changed
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

    // Optional confirmation but i fell this would get really annoying
    // if (QMessageBox::question(this, "Delete Note",
    //                           "Are you sure you want to delete this note?")
    //     != QMessageBox::Yes)
    //     return;

    notes_.remove(noteIndex);

    // Rebuild visuals and list
    updateNoteItems(currentX0, currentX1);
    refreshNotesList();
    plot_->replot();
}

void ECGViewer::onSaveNotes(const bool guiSave)
{
    if (notes_.isEmpty() && guiSave) {
        QMessageBox::information(this, "Save Notes", "There are no notes to save.");
        return;
    }
    if (!guiSave) {
        // save to default location without asking
        QVector<double> vals;
        QVector<double> times;
        // check if data folder is there if not make one
        QString dataFolderPath = "./ECGData";
        QDir dir(dataFolderPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        dir.setPath(dataFolderPath);
        std::cout << "Data folder path: " << dataFolderPath.toStdString() << std::endl;
        // get current folder path as a variable with gui
        QString fileNameNotes = dir.filePath(QString("%1_ecg_data.json").arg(filePrefix_));
        std::cout << "Saving to file: " << fileNameNotes.toStdString() << std::endl;
        QFile file(fileNameNotes);
        QJsonArray arr;
        for (const auto& n : notes_) {
            QJsonObject o;
            o["tag"] = n.tag;
            o["detail"] = n.detail;
            o["time"] = n.time;
            o["duration"] = n.duration;
            o["volts"] = n.volts;
            // o["type"] = static_cast<int>(n.type);
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
    if (fi.suffix().isEmpty()) {
        fileName += ".json";
    }

    QJsonArray arr;
    for (const auto& n : notes_) {
        QJsonObject o;
        o["tag"] = n.tag;
        o["detail"] = n.detail;
        o["time"] = n.time;
        o["volts"] = n.volts;
        o["duration"] = n.duration;
        // o["type"] = static_cast<int>(n.type);
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
    QVector<double> vals;
    QVector<double> times;
    if (!notes_.isEmpty()) {
        onSaveNotes(false); // save notes to default location
    }
    // check if data folder is there if not make one
    QString dataFolderPath = "./ECGData";
    QDir dir(dataFolderPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    dir.setPath(dataFolderPath);
    std::cout << "Data folder path: " << dataFolderPath.toStdString() << std::endl;
    // get current folder path as a variable with gui
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
        vals = valsFor(type);
        times = timesFor(type);
        QChar tagChar;
        switch (type) {
        case FiducialType::P: tagChar = 'P'; break;
        case FiducialType::Q: tagChar = 'Q'; break;
        case FiducialType::R: tagChar = 'R'; break;
        case FiducialType::S: tagChar = 'S'; break;
        case FiducialType::T: tagChar = 'T'; break;
        }
        for (int i = 0; i < times.size(); ++i) {
            // use string for tag
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
    // check file name against the data prefix. if it doesn't match, warn the user
    if (!QFileInfo(f).fileName().startsWith(filePrefix_)) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Load Notes",
                                      "The selected notes file does not match the current ECG data prefix.\n"
                                      "Are you sure you want to load it?",
                                      QMessageBox::Yes|QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
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
        if (n.duration < 0.0) n.duration = 0.0;
        if (n.time + n.duration > total_time_)
            n.duration = std::max(0.0, total_time_ - n.time);
        // n.type = static_cast<NoteType>(o.value("type").toInt());

        // Clamp time into [0, total_time_]
        if (n.time < 0.0) n.time = 0.0;
        if (n.time > total_time_) n.time = total_time_;

        loaded.push_back(n);
    }

    notes_ = loaded;

    // Refresh visuals & list
    updateNoteItems(currentX0, currentX1);
    refreshNotesList();
    plot_->replot();
}

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

            if (!filter.isEmpty()) {
                QString haystack = n.tag + " " + n.detail;
                if (!haystack.contains(filter, Qt::CaseInsensitive))
                    continue;
            }

            QListWidgetItem* item = new QListWidgetItem(line, list);
            item->setData(Qt::UserRole, i);  // store note index
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

    // New note at centre of current window
    QObject::connect(btnNew, &QPushButton::clicked,
                     &dlg, [this, refreshList]()
    {
        double newTime = 0.5 * (currentX0 + currentX1);
        if (newTime < 0.0) newTime = 0.0;
        if (newTime > total_time_) newTime = total_time_;

        double absTime = t_.first() + newTime;
        int sampleIndex = static_cast<int>(std::round((absTime - t_.first()) * fs_));
        if (sampleIndex < 0) sampleIndex = 0;
        if (sampleIndex >= vClean_.size()) sampleIndex = vClean_.size() - 1;
        double val = vClean_[sampleIndex];

        Note n;
        n.time = newTime;
        n.volts = val;
        n.tag = QStringLiteral("Note %1").arg(notes_.size() + 1);
        notes_.push_back(n);

        openNoteEditor(notes_.size() - 1);     // reuse existing editor
        updateNoteItems(currentX0, currentX1); // redraw on plot
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

    // Double-click list: jump to note + edit
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

    // Initial populate
    refreshList();

    dlg.exec();  // modal
}

} // namespace ECGViewer