#include "ECGViewer.hpp"

#include <QString>
#include <QInputDialog>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>

// Pretty empty file so far but going to be used for notes etc etc
namespace ECGViewer {
void ECGViewer::onInsertManualFiducial()
{
    QString choice = manualTypeCombo_ ? manualTypeCombo_->currentText() : QString("R");

    FiducialType type = FiducialType::R;
    if (choice == "P")      type = FiducialType::P;
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

    // Replot so the new fiducial appears (and is draggable using your existing logic)
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
    n.time  = newTime;
    n.volts = val;
    n.tag   = QStringLiteral("Note %1").arg(notes_.size() + 1);
    n.detail = QString();

    notes_.push_back(n);

    // Refresh visuals
    updateNoteItems(currentX0, currentX1);
    plot_->replot();

    // Optionally open editor immediately:
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

    auto* voltsSpin = new QDoubleSpinBox(&dlg);
    voltsSpin->setDecimals(5);
    // a bit generous; adapt as needed
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
        n.tag   = tagEdit->text();
        n.time  = timeSpin->value();
        n.volts = voltsSpin->value();
        n.detail = detailEdit->toPlainText();

        // After editing time, ensure it's clamped
        if (n.time < 0.0) n.time = 0.0;
        if (n.time > total_time_) n.time = total_time_;

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


} // namespace ECGViewer